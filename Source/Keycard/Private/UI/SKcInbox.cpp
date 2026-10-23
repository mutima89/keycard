#include "SKcInbox.h"

#include "KcCampaignSubsystem.h"
#include "KcConvert.h"
#include "KcGameSubsystem.h"
#include "KcUIStyle.h"
#include "Engine/GameInstance.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Keycard"

using namespace KcUI;
using KcConvert::ToFString;
using KcConvert::ToStd;

namespace
{
FOnClicked InboxClick(TFunction<void()> Fn)
{
	return FOnClicked::CreateLambda([Fn]() { Fn(); return FReply::Handled(); });
}

TSharedRef<SWidget> InboxText(const FString& Text, int32 Size = 14, const FLinearColor& Color = TextPrimary, EWeight Weight = EWeight::Regular)
{
	return SNew(STextBlock).Text(FText::FromString(Text)).Font(FontW(Size, Weight)).ColorAndOpacity(Color).AutoWrapText(true);
}

FLinearColor CategoryColor(kc::EMessageCategory Category)
{
	switch (Category)
	{
	case kc::EMessageCategory::Incident: return Bad;
	case kc::EMessageCategory::Story: return FLinearColor(0.70f, 0.55f, 0.95f);
	case kc::EMessageCategory::Personal: return FLinearColor(0.95f, 0.62f, 0.55f);
	case kc::EMessageCategory::Politics: return FLinearColor(0.85f, 0.75f, 0.45f);
	case kc::EMessageCategory::Request: return Warning;
	default: return Accent;
	}
}

FString DeadlineText(int32 WeeksLeft)
{
	if (WeeksLeft <= 0) return TEXT("Reply this week");
	return WeeksLeft == 1 ? FString(TEXT("Reply within 1 week")) : FString::Printf(TEXT("Reply within %d weeks"), WeeksLeft);
}

const kc::GameState* StateOf(const TWeakObjectPtr<UKcCampaignSubsystem>& Campaign)
{
	const UGameInstance* GI = Campaign.IsValid() ? Campaign->GetGameInstance() : nullptr;
	const UKcGameSubsystem* G = GI ? GI->GetSubsystem<UKcGameSubsystem>() : nullptr;
	return G && G->HasGame() ? &G->GetState() : nullptr;
}

// Messages needing a reply first (soonest deadline), then unread, then newest.
TArray<const kc::InboxMessage*> Sorted(const kc::GameState* S)
{
	TArray<const kc::InboxMessage*> Out;
	if (!S) return Out;
	for (const kc::InboxMessage& M : S->World.Inbox) Out.Add(&M);
	Out.StableSort([](const kc::InboxMessage& A, const kc::InboxMessage& B)
	{
		if (A.NeedsReply() != B.NeedsReply()) return A.NeedsReply();
		if (A.NeedsReply() && A.DeadlineWeek != B.DeadlineWeek) return A.DeadlineWeek < B.DeadlineWeek;
		if (A.bRead != B.bRead) return !A.bRead;
		return A.Week > B.Week;
	});
	return Out;
}
} // namespace

void SKcInbox::Construct(const FArguments& InArgs)
{
	Campaign = InArgs._Campaign;
	OnClose = InArgs._OnClose;
	TSharedPtr<SButton> Close;
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[MakeEyebrow(LOCTEXT("InboxEyebrow", "LP office  /  Inbox"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, Space1, 0.0f, Space3)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)[MakeHeading(LOCTEXT("InboxTitle", "Inbox"))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(FontW(13, EWeight::Medium)).ColorAndOpacity(TextMuted)
				.Text_Lambda([this]()
				{
					const kc::GameState* S = StateOf(Campaign);
					if (!S) return FText::GetEmpty();
					int32 Unread = 0, Awaiting = 0;
					for (const kc::InboxMessage& M : S->World.Inbox)
					{
						Unread += M.bRead ? 0 : 1;
						Awaiting += M.NeedsReply() ? 1 : 0;
					}
					return FText::Format(LOCTEXT("InboxCounts", "Week {0}  |  {1} unread  |  {2} awaiting your reply"), S->Property.Week, Unread, Awaiting);
				})
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.42f).Padding(0.0f, 0.0f, Space3, 0.0f)
			[
				SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(ListBox, SBox)]
			]
			+ SHorizontalBox::Slot().FillWidth(0.58f)
			[
				SNew(SBorder).BorderImage(CardBrush()).Padding(Space6)
				[
					SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(ReaderBox, SBox)]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, Space3, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text_Lambda([this]() { return Status; }).Font(FontW(13, EWeight::Medium)).ColorAndOpacity(Warning).AutoWrapText(true)
			]
			+ SHorizontalBox::Slot().AutoWidth()[MakeButton(LOCTEXT("InboxClose", "Close"), InboxClick([this]() { OnClose(); }), &Close)]
		]
	];
	FirstFocus = Close;
	if (!InArgs._OpenMessageId.IsEmpty())
	{
		Select(InArgs._OpenMessageId);
	}
	else
	{
		const TArray<const kc::InboxMessage*> Inbox = Sorted(StateOf(Campaign));
		if (!Inbox.IsEmpty()) Select(ToFString(Inbox[0]->Id)); // the most pressing message, so the reader is never empty
	}
	Rebuild();
}

void SKcInbox::Select(const FString& MessageId)
{
	Selected = MessageId;
	Status = FText::GetEmpty();
	if (Campaign.IsValid()) Campaign->MarkMessageRead(MessageId);
	Rebuild();
}

bool SKcInbox::Reply(const FString& ChoiceId)
{
	if (!Campaign.IsValid()) return false;
	FString Error;
	const bool bOk = Campaign->ReplyToMessage(Selected, ChoiceId, Error) == EKcCaseResult::Ok;
	Status = bOk ? FText::GetEmpty() : FText::FromString(Error);
	Rebuild();
	return bOk;
}

void SKcInbox::Rebuild()
{
	if (!ListBox.IsValid() || !ReaderBox.IsValid()) return;
	ListBox->SetContent(BuildList());
	ReaderBox->SetContent(BuildReader());
}

TSharedRef<SWidget> SKcInbox::BuildList()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	const kc::GameState* S = StateOf(Campaign);
	const TArray<const kc::InboxMessage*> Inbox = Sorted(S);
	if (Inbox.IsEmpty())
	{
		Box->AddSlot().AutoHeight()[InboxText(TEXT("No messages yet. People will write once your work starts to land."), 14, TextMuted)];
		return Box;
	}
	for (const kc::InboxMessage* M : Inbox)
	{
		const FString Id = ToFString(M->Id);
		const bool bSelected = Id == Selected;
		const FLinearColor Cat = CategoryColor(M->Category);
		FString Meta = FString::Printf(TEXT("%s  |  week %d"), *ToFString(kc::ToString(M->Category)).ToUpper(), M->Week);
		if (M->NeedsReply()) Meta += TEXT("  |  ") + DeadlineText(M->DeadlineWeek - S->Property.Week);
		Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, Space2)
		[
			SNew(SButton)
			.ButtonColorAndOpacity(bSelected ? FLinearColor(Accent.R, Accent.G, Accent.B, 0.35f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.06f))
			.ContentPadding(FMargin(Space3, Space2))
			.OnClicked(InboxClick([this, Id]() { Select(Id); }))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, Space3, 0.0f)
				[
					SNew(SBox).WidthOverride(4.0f)[SNew(SBorder).BorderImage(FocusBrush()).BorderBackgroundColor(Cat)]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						InboxText((M->bRead ? FString() : FString(TEXT("●  "))) + Campaign->PersonName(M->From), 13, M->bRead ? TextMuted : TextPrimary, EWeight::SemiBold)
					]
					+ SVerticalBox::Slot().AutoHeight()[InboxText(ToFString(M->Subject), 14, TextPrimary, M->bRead ? EWeight::Regular : EWeight::Medium)]
					+ SVerticalBox::Slot().AutoHeight()[InboxText(Meta, 11, M->NeedsReply() ? Warning : TextMuted, EWeight::Medium)]
				]
			]
		];
	}
	return Box;
}

TSharedRef<SWidget> SKcInbox::BuildReader()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	const kc::GameState* S = StateOf(Campaign);
	const kc::InboxMessage* M = nullptr;
	for (const kc::InboxMessage* X : Sorted(S))
	{
		if (ToFString(X->Id) == Selected) M = X;
	}
	if (!M)
	{
		Box->AddSlot().AutoHeight()[InboxText(TEXT("Select a message."), 14, TextMuted)];
		return Box;
	}
	const kc::EventDef* Event = Campaign->GetWorldContent().Events.Find(M->EventId);
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, Space1)[InboxText(ToFString(kc::ToString(M->Category)).ToUpper(), 11, CategoryColor(M->Category), EWeight::Bold)];
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, Space1)[InboxText(ToFString(M->Subject), 20, TextPrimary, EWeight::SemiBold)];
	const FString Name = Campaign->PersonName(M->From), Role = Campaign->PersonRole(M->From);
	const FString From = Role.IsEmpty() ? Name : FString::Printf(TEXT("%s, %s"), *Name, *Role);
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, Space4)[InboxText(FString::Printf(TEXT("From %s  |  week %d"), *From, M->Week), 13, TextMuted)];
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, Space4)[InboxText(ToFString(M->Body), 15)];
	if (M->NeedsReply() && Event)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, Space2, 0.0f, Space2)
		[
			InboxText(DeadlineText(M->DeadlineWeek - S->Property.Week) + TEXT(". If you do not reply, events will decide for you."), 13, Warning, EWeight::Medium)
		];
		bool bFirst = true;
		for (const kc::EventChoiceDef& C : Event->Choices)
		{
			const FString ChoiceId = ToFString(C.Id);
			const bool bLocked = !C.RequiresSkill.empty() && !kc::HasSkill(*S, C.RequiresSkill);
			TSharedPtr<SButton> Button;
			Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, Space2)
			[
				MakeButton(FText::FromString(ToFString(C.Label)), InboxClick([this, ChoiceId]() { Reply(ChoiceId); }), &Button, !bLocked)
			];
			if (bFirst) FirstFocus = Button;
			bFirst = false;
		}
	}
	else if (!M->Outcome.empty())
	{
		FString Chosen = ToFString(M->ChoiceId);
		if (Event)
		{
			for (const kc::EventChoiceDef& C : Event->Choices)
			{
				if (C.Id == M->ChoiceId) Chosen = ToFString(C.Label);
			}
		}
		Box->AddSlot().AutoHeight().Padding(0.0f, Space2, 0.0f, Space1)
		[
			InboxText(M->bAutoResolved ? FString(TEXT("You did not reply in time")) : FString::Printf(TEXT("You replied: %s"), *Chosen), 13, M->bAutoResolved ? Warning : Accent, EWeight::SemiBold)
		];
		Box->AddSlot().AutoHeight()[InboxText(ToFString(M->Outcome), 14, TextMuted)];
	}
	return Box;
}

#undef LOCTEXT_NAMESPACE
