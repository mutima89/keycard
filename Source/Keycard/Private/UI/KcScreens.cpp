#include "KcScreens.h"

#include <algorithm>
#include "Algo/Reverse.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformProcess.h"
#include "KcCampaignSubsystem.h"
#include "KcConvert.h"
#include "KcCredits.h"
#include "KcGameSubsystem.h"
#include "KcHUD.h"
#include "KcHotel.h"
#include "KcSaveSubsystem.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Keycard"

using namespace KcUI;
using KcConvert::ToFString;
using KcConvert::ToStd;

namespace
{
TSharedRef<SVerticalBox> Column() { return SNew(SVerticalBox); }

void AddRow(const TSharedRef<SVerticalBox>& Box, TSharedRef<SWidget> W, float Bottom = 10.0f)
{
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, Bottom)[W];
}

void AddGap(const TSharedRef<SVerticalBox>& Box, float Height = 10.0f)
{
	Box->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(Height)];
}

// Wrapped at a fixed width: rows far down a scrolled page wrap correctly before they are first painted.
TSharedRef<SWidget> WrappedLine(const FString& Text, int32 Size, const FLinearColor& Color, float WrapAt = 720.0f)
{
	return SNew(STextBlock).Text(FText::FromString(Text)).Font(FontW(Size, EWeight::Regular)).ColorAndOpacity(Color).WrapTextAt(WrapAt).LineHeightPercentage(1.15f);
}

void AddBullets(const TSharedRef<SVerticalBox>& Box, const FText& Heading, const TArray<FString>& Items, const FLinearColor& Color = TextPrimary, bool bNumbered = false)
{
	if (Items.IsEmpty()) return;
	AddRow(Box, MakeHeading(Heading, 16), 4.0f);
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		AddRow(Box, WrappedLine((bNumbered ? FString::Printf(TEXT("%d.  "), i + 1) : FString(TEXT("-  "))) + Items[i], 14, Color), 3.0f);
	}
	AddGap(Box, 8.0f);
}

TArray<FString> ToLines(const std::vector<std::string>& In) { return KcConvert::ToFStrings(In); }

FOnClicked Click(TFunction<void()> Fn)
{
	return FOnClicked::CreateLambda([Fn]() { if (Fn) Fn(); return FReply::Handled(); });
}

TSharedRef<SWidget> Scrolling(TSharedRef<SWidget> Content)
{
	return SNew(SScrollBox) + SScrollBox::Slot()[Content];
}

FText Ratio2(double Value)
{
	FNumberFormattingOptions O;
	O.MinimumFractionalDigits = 2;
	O.MaximumFractionalDigits = 2;
	return FText::AsNumber(Value, &O);
}

UKcCampaignSubsystem* CampaignOf(const AKcHUD* Hud) { return Hud ? Hud->Campaign() : nullptr; }

const FSliderStyle& SliderStyle()
{
	static FSliderStyle Style = []
	{
		FSliderStyle S = FCoreStyle::Get().GetWidgetStyle<FSliderStyle>("Slider");
		const FSlateColorBrush Bar(FLinearColor(1.0f, 1.0f, 1.0f, 0.22f));
		const FSlateRoundedBoxBrush Handle(Accent, 7.0f, FVector2f(14.0f, 14.0f));
		S.SetNormalBarImage(Bar).SetHoveredBarImage(Bar).SetDisabledBarImage(Bar);
		S.SetNormalThumbImage(Handle).SetHoveredThumbImage(Handle).SetDisabledThumbImage(Handle);
		S.SetBarThickness(3.0f);
		return S;
	}();
	return Style;
}

TSharedRef<SWidget> SettingSlider(const FText& Label, float Min, float Max, float Step, TFunction<float()> Get, TFunction<void(float)> Set)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(220.0f)[MakeBody(Label, 15)]]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(SSlider).MinValue(Min).MaxValue(Max).StepSize(Step).IsFocusable(true).Style(&SliderStyle())
			.Value_Lambda([Get]() { return Get(); })
			.OnValueChanged_Lambda([Set](float V) { Set(V); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox).WidthOverride(60.0f)[SNew(STextBlock).Font(Font(15)).ColorAndOpacity(TextPrimary).Text_Lambda([Get]() { return Ratio2(Get()); })]
		];
}

TSharedRef<SWidget> SettingToggle(const FText& Label, TFunction<bool()> Get, TFunction<void(bool)> Set)
{
	return SNew(SCheckBox).Style(&KcUI::CheckStyle())
		.IsChecked_Lambda([Get]() { return Get() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([Set](ECheckBoxState S) { Set(S == ECheckBoxState::Checked); })
		[MakeBody(Label, 15)];
}

// A row of radio buttons for an int setting.
TSharedRef<SWidget> SettingRadio(const FText& Label, const TArray<TPair<int32, FText>>& Values, TFunction<int32()> Get, TFunction<void(int32)> Set)
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(220.0f)[MakeBody(Label, 15)]];
	for (const TPair<int32, FText>& V : Values)
	{
		const int32 Value = V.Key;
		Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, Space4, 0.0f)
		[
			SNew(SCheckBox).Style(&KcUI::RadioStyle())
			.IsChecked_Lambda([Get, Value]() { return Get() == Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([Set, Value](ECheckBoxState) { Set(Value); })
			[MakeBody(V.Value, 14)]
		];
	}
	return Row;
}

// A table for tool views (lock audit trail, CCTV clips, key log). The first row is the header.
TSharedRef<SWidget> Table(const std::vector<std::string>& Columns, const std::vector<std::vector<std::string>>& Rows)
{
	TSharedRef<SVerticalBox> Box = Column();
	auto MakeRow = [&](const std::vector<std::string>& Cells, bool bHeader, int32 Index)
	{
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (size_t c = 0; c < Columns.size(); ++c)
		{
			const FString Cell = c < Cells.size() ? ToFString(Cells[c]) : FString();
			Row->AddSlot().FillWidth(c + 1 == Columns.size() ? 2.0f : 1.0f).Padding(0.0f, 0.0f, Space3, 0.0f)
			[
				SNew(STextBlock).Text(FText::FromString(Cell)).AutoWrapText(true)
				.Font(bHeader ? FontW(12, EWeight::SemiBold) : FontW(13, EWeight::Regular))
				.ColorAndOpacity(bHeader ? TextMuted : TextPrimary)
			];
		}
		const FLinearColor Fill = bHeader ? FLinearColor(1, 1, 1, 0.07f) : (Index % 2 ? FLinearColor(1, 1, 1, 0.025f) : FLinearColor::Transparent);
		Box->AddSlot().AutoHeight()
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Fill).Padding(FMargin(Space2, Space1))[Row]
		];
	};
	MakeRow(Columns, true, 0);
	for (size_t r = 0; r < Rows.size(); ++r) MakeRow(Rows[r], false, static_cast<int32>(r));
	return SNew(SBorder).BorderImage(CardBrush()).Padding(Space2)[Box];
}

FString ToolTitle(kc::EActionKind K)
{
	switch (K)
	{
	case kc::EActionKind::LockAudit: return TEXT("Lock audit trail");
	case kc::EActionKind::CctvReview: return TEXT("CCTV review");
	case kc::EActionKind::ExportFootage: return TEXT("Footage export");
	case kc::EActionKind::Photograph: return TEXT("Scene photographs");
	case kc::EActionKind::BagEvidence: return TEXT("Evidence bag and label");
	case kc::EActionKind::LogEvidence: return TEXT("Evidence log");
	case kc::EActionKind::Check: return TEXT("Records check");
	case kc::EActionKind::Call: return TEXT("Radio and phone");
	case kc::EActionKind::Secure: return TEXT("Scene secured");
	case kc::EActionKind::Interview: return TEXT("Interview notes");
	case kc::EActionKind::Search: return TEXT("Search");
	case kc::EActionKind::Detain: return TEXT("Hold");
	case kc::EActionKind::Enter: return TEXT("Room entry");
	default: return TEXT("Notebook");
	}
}

// ---- Answer editors, shared by in-person questions and the report form ----
// Answer holds option ids (Single: one, Multi: any, Order: every one in order) or one figure (Numeric).
TSharedRef<SWidget> AnswerEditor(UKcCampaignSubsystem* C, const kc::QuestionDef& Q, TFunction<TArray<FString>&()> Answer)
{
	TSharedRef<SVerticalBox> Box = Column();
	const TArray<const kc::OptionDef*> Options = C ? C->OptionsInOrder(Q) : TArray<const kc::OptionDef*>();
	switch (Q.Kind)
	{
	case kc::EQuestionKind::Single:
	case kc::EQuestionKind::Multi:
	{
		const bool bMulti = Q.Kind == kc::EQuestionKind::Multi;
		if (bMulti) AddRow(Box, MakeBody(LOCTEXT("PickAll", "Pick all that apply."), 12, TextMuted), 4.0f);
		for (const kc::OptionDef* O : Options)
		{
			const FString Id = ToFString(O->Id);
			AddRow(Box, SNew(SCheckBox).Style(bMulti ? &KcUI::CheckStyle() : &KcUI::RadioStyle())
				.IsChecked_Lambda([Answer, Id]() { return Answer().Contains(Id) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([Answer, Id, bMulti](ECheckBoxState S)
				{
					TArray<FString>& A = Answer();
					if (!bMulti) { A = {Id}; return; }
					if (S == ECheckBoxState::Checked) A.AddUnique(Id);
					else A.Remove(Id);
				})
				[WrappedLine(ToFString(O->Label), 14, TextPrimary, 680.0f)], 4.0f);
		}
		break;
	}
	case kc::EQuestionKind::Order:
	{
		AddRow(Box, MakeBody(LOCTEXT("PickOrder", "Pick the steps in the order you would take them."), 12, TextMuted), 4.0f);
		AddRow(Box, SNew(STextBlock).AutoWrapText(true).Font(FontW(14, EWeight::Regular)).ColorAndOpacity(Accent).Text_Lambda([Answer, Options]()
		{
			FString S;
			int32 n = 1;
			for (const FString& Id : Answer())
			{
				for (const kc::OptionDef* O : Options)
				{
					if (ToFString(O->Id) == Id) S += FString::Printf(TEXT("%d. %s\n"), n++, *ToFString(O->Label));
				}
			}
			return FText::FromString(S.IsEmpty() ? FString(TEXT("(nothing yet)")) : S.TrimEnd());
		}), 6.0f);
		for (const kc::OptionDef* O : Options)
		{
			const FString Id = ToFString(O->Id);
			AddRow(Box, MakeButton(FText::FromString(ToFString(O->Label)), Click([Answer, Id]() { Answer().AddUnique(Id); }), nullptr,
				TAttribute<bool>::CreateLambda([Answer, Id]() { return !Answer().Contains(Id); })), 4.0f);
		}
		AddRow(Box, MakeButton(LOCTEXT("ClearOrder", "Start the order again"), Click([Answer]() { Answer().Reset(); })), 4.0f);
		break;
	}
	case kc::EQuestionKind::Numeric:
	{
		AddRow(Box, SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SEditableTextBox).Font(Font(15))
				.Text_Lambda([Answer]() { const TArray<FString>& A = Answer(); return FText::FromString(A.IsEmpty() ? FString() : A[0]); })
				.OnTextChanged_Lambda([Answer](const FText& T)
				{
					TArray<FString>& A = Answer();
					const FString V = T.ToString().TrimStartAndEnd();
					if (V.IsEmpty()) A.Reset();
					else A = {V};
				})
				.HintText(LOCTEXT("FigureHint", "Your figure"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Space2, 0.0f, 0.0f, 0.0f)[MakeBody(FText::FromString(ToFString(Q.Unit)), 14, TextMuted)], 4.0f);
		break;
	}
	}
	return Box;
}

bool IsAnswered(const kc::QuestionDef& Q, const TArray<FString>& A)
{
	if (Q.Kind == kc::EQuestionKind::Order) return A.Num() == static_cast<int32>(Q.Options.size());
	return !A.IsEmpty();
}

FString SectionTitle(const std::string& S)
{
	if (S == "who") return TEXT("Who");
	if (S == "what") return TEXT("What happened");
	if (S == "when") return TEXT("When");
	if (S == "where") return TEXT("Where");
	if (S == "how") return TEXT("How");
	if (S == "actions") return TEXT("Actions taken");
	if (S == "facts") return TEXT("Facts established");
	return TEXT("Decisions and recommendations");
}

FString ScoreWord(int32 Score)
{
	if (Score < 0) return TEXT("not assessed");
	if (Score >= 85) return FString::Printf(TEXT("%d  strong"), Score);
	if (Score >= 60) return FString::Printf(TEXT("%d  sound"), Score);
	return FString::Printf(TEXT("%d  needs work"), Score);
}
} // namespace

namespace KcScreens
{
// ---------------------------------------------------------------- menus

FKcScreenParts MainMenu(AKcHUD* Hud)
{
	TSharedPtr<SButton> First;
	const bool bHasSave = Hud->Saves()->HasSave();
	TSharedRef<SVerticalBox> Box = Column();
	AddRow(Box, MakeEyebrow(LOCTEXT("Studio", "Veldane Grand Harbourfront  /  Loss Prevention")), Space2);
	AddRow(Box, SNew(STextBlock).Text(LOCTEXT("GameTitle", "KEYCARD")).Font(FontW(52, EWeight::Bold)).ColorAndOpacity(TextPrimary), 2.0f);
	AddRow(Box, MakeBody(LOCTEXT("Tagline", "From the night shift to Vice President, Global Safety & Security. Observe, report, de-escalate."), 16, TextMuted), 24.0f);
	if (bHasSave)
	{
		AddRow(Box, MakeButton(LOCTEXT("Continue", "Continue"), Click([Hud]()
		{
			FString Message;
			const EKcLoadOutcome Outcome = Hud->ContinueGame(Message);
			if (Outcome != EKcLoadOutcome::Ok && Outcome != EKcLoadOutcome::RecoveredFromBackup) Hud->ShowMainMenu(); // shows MenuMessage
		}), &First, true, true), 8.0f);
	}
	AddRow(Box, MakeButton(LOCTEXT("NewGame", "New career"), Click([Hud]() { Hud->ShowDifficultyPicker(); }), bHasSave ? nullptr : &First, true, !bHasSave), 8.0f);
	AddRow(Box, MakeButton(LOCTEXT("Settings", "Settings"), Click([Hud]() { Hud->OpenSettings([Hud]() { Hud->ShowMainMenu(); }); })), 8.0f);
	AddRow(Box, MakeButton(LOCTEXT("MenuCredits", "Credits"), Click([Hud]() { Hud->OpenCredits([Hud]() { Hud->ShowMainMenu(); }); })), 8.0f);
	AddRow(Box, MakeButton(LOCTEXT("Quit", "Quit"), Click([Hud]() { Hud->QuitToDesktop(); })), 16.0f);
	if (!Hud->MenuMessage.IsEmpty()) AddRow(Box, MakeBody(Hud->MenuMessage, 14, Bad));
	AddRow(Box, MakeBody(LOCTEXT("Fiction", "A training-style fiction. Every business and person is invented; procedures follow good practice, not any one law. Not legal advice."), 12, TextMuted), 0.0f);
	return {MakeModalFrame(Box, 640.0f), First};
}

FKcScreenParts DifficultyPicker(AKcHUD* Hud)
{
	struct FChoiceRow
	{
		EKcDifficulty Difficulty;
		FText Label;
		FText Help;
	};
	const FChoiceRow Rows[] = {
		{EKcDifficulty::Trainee, LOCTEXT("Trainee", "Trainee"), LOCTEXT("TraineeHelp", "Hints on: the notebook shows where work is left. Fewer things go wrong between shifts.")},
		{EKcDifficulty::Professional, LOCTEXT("Professional", "Professional"), LOCTEXT("ProfessionalHelp", "The intended experience. Hints on, adjustable in settings.")},
		{EKcDifficulty::Expert, LOCTEXT("Expert", "Expert"), LOCTEXT("ExpertHelp", "No hints. Weak decisions come back more often.")},
	};
	TSharedPtr<SButton> First;
	TSharedRef<SVerticalBox> Box = Column();
	AddRow(Box, MakeEyebrow(LOCTEXT("DiffEyebrow", "New career")), Space1);
	AddRow(Box, MakeHeading(LOCTEXT("ChooseDifficulty", "Choose difficulty")), 4.0f);
	AddRow(Box, MakeBody(LOCTEXT("DifficultyNote", "Difficulty changes hints and how often trouble returns, never how your work is judged."), 14, TextMuted), 16.0f);
	for (const FChoiceRow& C : Rows)
	{
		const EKcDifficulty D = C.Difficulty;
		AddRow(Box, MakeButton(C.Label, Click([Hud, D]() { Hud->ShowBackgroundPicker(D); }), D == EKcDifficulty::Professional ? &First : nullptr, true, D == EKcDifficulty::Professional), 2.0f);
		AddRow(Box, MakeBody(C.Help, 13, TextMuted), 12.0f);
	}
	AddRow(Box, MakeButton(LOCTEXT("Back", "Back"), Click([Hud]() { Hud->ShowMainMenu(); })), 0.0f);
	return {MakeModalFrame(Box, 640.0f), First};
}

FKcScreenParts BackgroundPicker(AKcHUD* Hud, EKcDifficulty Difficulty)
{
	struct FBg
	{
		const TCHAR* Id;
		kc::EBackground Sim;
	};
	const FBg Backgrounds[] = {
		{TEXT("front_office"), kc::EBackground::FrontOffice},
		{TEXT("ex_police"), kc::EBackground::ExPolice},
		{TEXT("military"), kc::EBackground::Military},
		{TEXT("criminology"), kc::EBackground::CriminologyGraduate},
	};
	TSharedPtr<SButton> First;
	TSharedRef<SVerticalBox> Box = Column();
	AddRow(Box, MakeEyebrow(LOCTEXT("BgEyebrow", "New career")), Space1);
	AddRow(Box, MakeHeading(LOCTEXT("ChooseBackground", "Where do you come from?")), 4.0f);
	AddRow(Box, MakeBody(LOCTEXT("BgNote", "Your background sets your starting strengths. Promotion still needs evidence from your work here: it never comes from where you started."), 14, TextMuted), 16.0f);
	for (const FBg& B : Backgrounds)
	{
		const kc::BackgroundDef& Def = kc::Background(B.Sim);
		const FString Id = B.Id;
		const bool bDefault = Id == TEXT("front_office");
		AddRow(Box, MakeButton(FText::FromString(ToFString(Def.Title)), Click([Hud, Difficulty, Id]() { Hud->StartNewGame(Difficulty, Id); }), bDefault ? &First : nullptr, true, bDefault), 2.0f);
		AddRow(Box, WrappedLine(ToFString(Def.Summary), 13, TextMuted), 12.0f);
	}
	AddRow(Box, MakeButton(LOCTEXT("BgBack", "Back"), Click([Hud]() { Hud->ShowDifficultyPicker(); })), 0.0f);
	return {MakeModalFrame(Box, 760.0f), First};
}

FKcScreenParts PauseMenu(AKcHUD* Hud)
{
	TSharedPtr<SButton> First;
	TSharedRef<SVerticalBox> Box = Column();
	AddRow(Box, MakeHeading(LOCTEXT("Paused", "Paused")), 16.0f);
	AddRow(Box, MakeButton(LOCTEXT("Resume", "Resume"), Click([Hud]() { Hud->CloseScreen(); }), &First, true, true), 8.0f);
	AddRow(Box, MakeButton(LOCTEXT("Save", "Save game"), Click([Hud]() { FString M; Hud->SaveNow(M); })), 8.0f);
	AddRow(Box, MakeButton(LOCTEXT("Journal", "Notebook"), Click([Hud]() { Hud->CloseScreen(); Hud->OpenJournal(); })), 8.0f);
	AddRow(Box, MakeButton(LOCTEXT("PauseSettings", "Settings"), Click([Hud]() { Hud->OpenSettings([Hud]() { Hud->CloseScreen(); Hud->OpenPauseMenu(); }); })), 8.0f);
	AddRow(Box, MakeButton(LOCTEXT("PauseCredits", "Credits"), Click([Hud]() { Hud->OpenCredits([Hud]() { Hud->CloseScreen(); Hud->OpenPauseMenu(); }); })), 8.0f);
	AddRow(Box, MakeButton(LOCTEXT("QuitDesktop", "Quit to desktop"), Click([Hud]() { Hud->QuitToDesktop(); })), 12.0f);
	AddRow(Box, MakeBody(LOCTEXT("PauseNote", "The game autosaves when a case starts and when a report is filed. Steps taken since the last autosave are kept only if you save here."), 12, TextMuted), 0.0f);
	return {MakeModalFrame(Box, 540.0f), First};
}

FKcScreenParts Settings(AKcHUD* Hud, TFunction<void()> OnBack)
{
	TWeakObjectPtr<UKcGameSubsystem> Game(Hud->Game());
	auto Get = [Game]() { return Game.IsValid() ? Game->GetSettings() : FKcSettingsView(); };
	auto Update = [Game](TFunction<void(FKcSettingsView&)> Change, bool bWindowMode)
	{
		if (!Game.IsValid()) return;
		FKcSettingsView S = Game->GetSettings();
		Change(S);
		Game->SetSettings(S, bWindowMode);
	};
	TSharedPtr<SButton> Back;
	TSharedRef<SVerticalBox> Box = Column();
	AddRow(Box, MakeHeading(LOCTEXT("SettingsTitle", "Settings")), 16.0f);
	AddRow(Box, SettingSlider(LOCTEXT("Sensitivity", "Mouse sensitivity"), 0.1f, 5.0f, 0.1f,
		[Get]() { return Get().MouseSensitivity; }, [Update](float V) { Update([V](FKcSettingsView& S) { S.MouseSensitivity = V; }, false); }));
	AddRow(Box, SettingSlider(LOCTEXT("PadSensitivity", "Controller look sensitivity"), 0.1f, 5.0f, 0.1f,
		[Get]() { return Get().GamepadSensitivity; }, [Update](float V) { Update([V](FKcSettingsView& S) { S.GamepadSensitivity = V; }, false); }));
	AddRow(Box, SettingToggle(LOCTEXT("InvertY", "Invert look up and down"),
		[Get]() { return Get().bInvertY; }, [Update](bool B) { Update([B](FKcSettingsView& S) { S.bInvertY = B; }, false); }));
	AddRow(Box, SettingSlider(LOCTEXT("TextSize", "Text size"), 0.8f, 1.6f, 0.1f,
		[Get]() { return Get().TextScale; }, [Update](float V) { Update([V](FKcSettingsView& S) { S.TextScale = V; }, false); }));
	AddRow(Box, SettingSlider(LOCTEXT("Volume", "Master volume"), 0.0f, 1.0f, 0.05f,
		[Get]() { return Get().MasterVolume; }, [Update](float V) { Update([V](FKcSettingsView& S) { S.MasterVolume = V; }, false); }));
	// Graphics quality: engine scalability presets. Low switches off Lumen for slower GPUs.
	AddRow(Box, SettingRadio(LOCTEXT("Quality", "Graphics quality"),
		{{0, LOCTEXT("QLow", "Low")}, {1, LOCTEXT("QMedium", "Medium")}, {2, LOCTEXT("QHigh", "High")}, {3, LOCTEXT("QEpic", "Epic")}},
		[Get]() { return Get().GraphicsQuality; }, [Update](int32 Q) { Update([Q](FKcSettingsView& S) { S.GraphicsQuality = Q; }, false); }));
	// Upscaling: TSR renders fewer pixels and reconstructs the image (more frames on slower GPUs).
	AddRow(Box, SettingRadio(LOCTEXT("Upscaling", "Upscaling (TSR)"),
		{{0, LOCTEXT("UNative", "Native")}, {1, LOCTEXT("UQuality", "Quality")}, {2, LOCTEXT("UBalanced", "Balanced")}, {3, LOCTEXT("UPerf", "Performance")}},
		[Get]() { return Get().Upscaling; }, [Update](int32 Q) { Update([Q](FKcSettingsView& S) { S.Upscaling = Q; }, false); }));
	AddRow(Box, SettingRadio(LOCTEXT("FrameCap", "Frame rate limit"),
		{{30, LOCTEXT("F30", "30")}, {60, LOCTEXT("F60", "60")}, {120, LOCTEXT("F120", "120")}, {0, LOCTEXT("FOff", "Unlimited")}},
		[Get]() { return Get().FrameCap; }, [Update](int32 Cap) { Update([Cap](FKcSettingsView& S) { S.FrameCap = Cap; }, false); }));
	AddRow(Box, SettingToggle(LOCTEXT("VSync", "Vertical sync"),
		[Get]() { return Get().bVSync; }, [Update](bool B) { Update([B](FKcSettingsView& S) { S.bVSync = B; }, false); }));
	AddRow(Box, SettingToggle(LOCTEXT("Fullscreen", "Fullscreen (off = windowed)"),
		// Shows the window as it is (a -windowed launch is windowed whatever the save says).
		[Get]()
		{
			const TSharedPtr<SWindow> W = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr;
			return W.IsValid() ? W->GetWindowMode() != EWindowMode::Windowed : Get().bFullscreen;
		},
		[Update](bool B) { Update([B](FKcSettingsView& S) { S.bFullscreen = B; }, true); }));
	AddRow(Box, SettingToggle(LOCTEXT("ReducedMotion", "Reduced motion (cuts instead of camera moves, no motion blur, no head bob)"),
		[Get]() { return Get().bReducedMotion; }, [Update](bool B) { Update([B](FKcSettingsView& S) { S.bReducedMotion = B; }, false); }));
	AddRow(Box, SettingToggle(LOCTEXT("ColourSafe", "Colour-safe palette (blue and orange for good and bad)"),
		[Get]() { return Get().bColourSafe; }, [Update](bool B) { Update([B](FKcSettingsView& S) { S.bColourSafe = B; }, false); }));
	AddRow(Box, SettingToggle(LOCTEXT("Hints", "Hints (the notebook lists the places where work is left)"),
		[Get]() { return Get().bHints; }, [Update](bool B) { Update([B](FKcSettingsView& S) { S.bHints = B; }, false); }));
	AddRow(Box, MakeBody(LOCTEXT("SettingsNote", "Settings are stored with your save. Text size applies to screens opened after this one. Left and Right adjust a focused slider."), 12, TextMuted), 12.0f);
	AddRow(Box, MakeButton(LOCTEXT("SettingsBack", "Back"), Click(OnBack), &Back), 0.0f);
	TSharedRef<SVerticalBox> Root = Column();
	Root->AddSlot().FillHeight(1.0f)[Scrolling(Box)];
	return {MakeModalFrame(Root, 800.0f), Back};
}

FKcScreenParts Credits(AKcHUD* Hud, TFunction<void()> OnBack)
{
	TSharedPtr<SButton> Back;
	TSharedRef<SVerticalBox> Box = Column();
	AddRow(Box, MakeEyebrow(LOCTEXT("CreditsEyebrow", "Keycard")), Space1);
	AddRow(Box, MakeHeading(LOCTEXT("CreditsTitle", "Credits")), Space4);
	for (const KcCredits::FSection& S : KcCredits::Sections())
	{
		AddRow(Box, MakeHeading(FText::FromString(S.Title), 17), Space1);
		for (const FString& Line : S.Lines) AddRow(Box, WrappedLine(Line, 14, S.Title == TEXT("Created by") ? TextPrimary : TextMuted), 2.0f);
		if (S.Title == TEXT("Created by"))
		{
			// Links are shown as text; the browser opens only on this explicit button press.
			AddRow(Box, MakeButton(LOCTEXT("OpenLinkedIn", "Open LinkedIn profile in browser"),
				Click([]() { FPlatformProcess::LaunchURL(KcCredits::CreatorLinkedIn, nullptr, nullptr); })), Space2);
		}
		AddGap(Box, Space3);
	}
	TSharedRef<SVerticalBox> Root = Column();
	Root->AddSlot().FillHeight(1.0f)[Scrolling(Box)];
	Root->AddSlot().AutoHeight().Padding(0.0f, Space3, 0.0f, 0.0f).HAlign(HAlign_Right)[MakeButton(LOCTEXT("CreditsBack", "Back"), Click(OnBack), &Back, true, true)];
	return {MakeModalFrame(Root, 900.0f), Back};
}

// ---------------------------------------------------------------- general

FKcScreenParts Document(const FText& Title, const FText& Subtitle, const FText& Body, TFunction<void()> OnClose)
{
	TSharedPtr<SButton> Close;
	TSharedRef<SVerticalBox> Root = Column();
	Root->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 2.0f)[MakeHeading(Title)];
	Root->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)[MakeBody(Subtitle, 13, TextMuted)];
	Root->AddSlot().FillHeight(1.0f)[SNew(SBorder).BorderImage(CardBrush()).Padding(16.0f)[Scrolling(MakeBody(Body, 16))]];
	Root->AddSlot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f).HAlign(HAlign_Right)[MakeButton(LOCTEXT("CloseDoc", "Close"), Click(OnClose), &Close)];
	return {MakeModalFrame(Root, 820.0f), Close};
}

FKcScreenParts Dialogue(const FText& Speaker, const FText& Text, TFunction<void()> OnContinue, const FText& ContinueLabel)
{
	TSharedPtr<SButton> Continue;
	TSharedRef<SVerticalBox> Box = Column();
	AddRow(Box, SNew(STextBlock).Text(Speaker).Font(Font(16, true)).ColorAndOpacity(Accent), 8.0f);
	AddRow(Box, MakeBody(Text, 17), 16.0f);
	Box->AddSlot().AutoHeight().HAlign(HAlign_Right)
	[
		MakeButton(ContinueLabel.IsEmpty() ? LOCTEXT("DialogueContinue", "Continue") : ContinueLabel, Click(OnContinue), &Continue, true, true)
	];
	return {MakeConversationFrame(Box, 900.0f), Continue};
}

FKcScreenParts Choices(const FText& Eyebrow, const FText& Title, const FText& Body, const TArray<FKcChoice>& Items, bool bConversation)
{
	TSharedPtr<SButton> First;
	TSharedRef<SVerticalBox> Box = Column();
	if (!Eyebrow.IsEmpty()) AddRow(Box, MakeEyebrow(Eyebrow), Space1);
	if (bConversation) AddRow(Box, SNew(STextBlock).Text(Title).Font(Font(16, true)).ColorAndOpacity(Accent), Space2);
	else AddRow(Box, MakeHeading(Title), Space2);
	if (!Body.IsEmpty()) AddRow(Box, MakeBody(Body, bConversation ? 17 : 14, bConversation ? TextPrimary : TextMuted), Space4);
	for (const FKcChoice& C : Items)
	{
		TSharedPtr<SButton>* Out = (!First.IsValid() && C.bEnabled) ? &First : nullptr;
		AddRow(Box, MakeButton(C.Label, Click(C.OnPick), Out, C.bEnabled, C.bPrimary), C.Note.IsEmpty() ? Space2 : 2.0f);
		if (!C.Note.IsEmpty()) AddRow(Box, MakeBody(C.Note, 12, TextMuted), Space2);
	}
	if (bConversation) return {MakeConversationFrame(Box, 900.0f), First};
	TSharedRef<SVerticalBox> Root = Column();
	Root->AddSlot().FillHeight(1.0f)[Scrolling(Box)];
	return {MakeModalFrame(Root, 760.0f), First};
}

// ---------------------------------------------------------------- casework

FKcScreenParts ActionResult(AKcHUD* Hud, const kc::ActionDef& A, TFunction<void()> OnDone)
{
	UKcCampaignSubsystem* C = CampaignOf(Hud);
	TSharedPtr<SButton> Done;
	TSharedRef<SVerticalBox> Box = Column();
	const FString Where = UKcCampaignSubsystem::LocationName(ToFString(A.At));
	const FString Clock = C ? C->ClockText() : FString();
	AddRow(Box, MakeEyebrow(FText::FromString(FString::Printf(TEXT("%s  /  %s"), *ToolTitle(A.Kind), *Where))), Space1);
	AddRow(Box, MakeHeading(FText::FromString(ToFString(A.Label))), 2.0f);
	AddRow(Box, MakeBody(FText::FromString(FString::Printf(TEXT("Finished at %s"), *Clock)), 13, TextMuted), Space4);
	if (!A.Columns.empty()) AddRow(Box, Table(A.Columns, A.Rows), Space4);
	const FString Finding = C ? C->FindingFor(A) : ToFString(A.Finding);
	if (!Finding.IsEmpty())
	{
		if (!A.FindingTitle.empty()) AddRow(Box, MakeHeading(FText::FromString(TEXT("Notebook: ") + ToFString(A.FindingTitle)), 16), 4.0f);
		AddRow(Box, SNew(SBorder).BorderImage(CardBrush()).Padding(Space4)[WrappedLine(Finding, 15, TextPrimary, 760.0f)], Space3);
	}
	if (!A.Evidence.empty())
	{
		AddRow(Box, MakeHeading(LOCTEXT("Custody", "Chain of custody"), 16), 4.0f);
		AddRow(Box, WrappedLine(FString::Printf(TEXT("%s  %s"), *Clock, *ToFString(A.Custody.empty() ? A.Evidence : A.Custody)), 14, TextMuted), Space3);
	}
	if (A.Cost > 0.0) AddRow(Box, MakeBody(FText::Format(LOCTEXT("ActionCost", "Committed spend: {0}"), Money(A.Cost)), 13, Warning), Space2);
	TSharedRef<SVerticalBox> Root = Column();
	Root->AddSlot().FillHeight(1.0f)[Scrolling(Box)];
	Root->AddSlot().AutoHeight().Padding(0.0f, Space3, 0.0f, 0.0f).HAlign(HAlign_Right)[MakeButton(LOCTEXT("ResultDone", "Done"), Click(OnDone), &Done, true, true)];
	return {MakeModalFrame(Root, 900.0f), Done};
}

FKcScreenParts Question(AKcHUD* Hud, const kc::QuestionDef& Q, const FText& PersonLine, TFunction<void(const TArray<FString>&)> OnCommit, TFunction<void()> OnBack)
{
	UKcCampaignSubsystem* C = CampaignOf(Hud);
	TSharedRef<TArray<FString>> Answer = MakeShared<TArray<FString>>();
	TSharedPtr<SButton> Later;
	TSharedRef<SVerticalBox> Box = Column();
	AddRow(Box, SNew(STextBlock).Text(PersonLine).Font(Font(16, true)).ColorAndOpacity(Accent), Space2);
	AddRow(Box, WrappedLine(ToFString(Q.Prompt), 17, TextPrimary, 840.0f), Space3);
	AddRow(Box, AnswerEditor(C, Q, [Answer]() -> TArray<FString>& { return *Answer; }), Space3);
	const kc::QuestionDef* QPtr = &Q;
	Box->AddSlot().AutoHeight().HAlign(HAlign_Right)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, Space2, 0.0f)[MakeButton(LOCTEXT("QBack", "Not yet"), Click(OnBack), &Later)]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeButton(LOCTEXT("QCommit", "Say it"), Click([Answer, OnCommit]() { OnCommit(*Answer); }), nullptr,
				TAttribute<bool>::CreateLambda([Answer, QPtr]() { return IsAnswered(*QPtr, *Answer); }), true)
		]
	];
	return {MakeConversationFrame(Box, 960.0f), Later};
}

FKcScreenParts ShiftBoard(AKcHUD* Hud, const FString& Where)
{
	UKcCampaignSubsystem* C = CampaignOf(Hud);
	const UKcGameSubsystem* G = Hud->Game();
	TArray<FKcChoice> Items;
	const kc::MissionDef* Next = C ? C->NextCase() : nullptr;
	if (Next)
	{
		const FString Id = ToFString(Next->Id);
		Items.Add({FText::FromString(FString::Printf(TEXT("Sign on for the shift: %s"), *ToFString(Next->Title))), [Hud, Id]() { Hud->StartCase(Id); }, true, true,
			LOCTEXT("SignOnNote", "Radio, keys and body-worn camera signed out. The first call comes in once you are on.")});
	}
	for (const kc::MissionDef* M : C ? C->AvailableCases() : TArray<const kc::MissionDef*>())
	{
		if (M == Next) continue;
		const FString Id = ToFString(M->Id);
		const bool bPassed = G->GetState().Career.HasPassed(M->Id);
		Items.Add({FText::FromString(FString::Printf(TEXT("%s: %s"), bPassed ? TEXT("Review an earlier case") : TEXT("Open case"), *ToFString(M->Title))),
			[Hud, Id]() { Hud->StartCase(Id); }, false, true, FText::GetEmpty()});
	}
	if (!Next) Items.Add({LOCTEXT("NoCases", "No cases open tonight"), nullptr, false, false, LOCTEXT("NoCasesNote", "Check your inbox: what happens next may be waiting on a reply.")});
	Items.Add({LOCTEXT("BoardInbox", "Inbox"), [Hud]() { Hud->CloseScreen(); Hud->OpenInbox(); }, false, true, FText::GetEmpty()});
	Items.Add({LOCTEXT("BoardManual", "Field Manual"), [Hud, Where]() { Hud->OpenFieldManual([Hud, Where]() { Hud->OpenTerminal(Where); }); }, false, true, FText::GetEmpty()});
	Items.Add({LOCTEXT("BoardCareer", "Career and promotion"), [Hud, Where]() { Hud->OpenCareer([Hud, Where]() { Hud->OpenTerminal(Where); }); }, false, true, FText::GetEmpty()});
	Items.Add({LOCTEXT("BoardLogOff", "Log off"), [Hud]() { Hud->CloseScreen(); }, false, true, FText::GetEmpty()});
	const FText Body = FText::Format(LOCTEXT("BoardBody", "{0}, level {1}. Week {2} at the Veldane Grand Harbourfront. Policy tonight as every night: observe, report and de-escalate."),
		FText::FromString(G->GetLevelTitle()), G->GetLevel(), G->GetState().Property.Week);
	return Choices(LOCTEXT("BoardEyebrow", "LP office terminal  /  Shift board"), LOCTEXT("BoardTitle", "Night shift, Loss Prevention"), Body, Items, false);
}

FKcScreenParts Report(AKcHUD* Hud, const FString& Where)
{
	UKcCampaignSubsystem* C = CampaignOf(Hud);
	const kc::MissionDef* M = C ? C->Active() : nullptr;
	if (!M || !C)
	{
		return Choices(FText::GetEmpty(), LOCTEXT("NoReport", "No case is open"), FText::GetEmpty(),
			{FKcChoice{LOCTEXT("NoReportBack", "Back"), [Hud]() { Hud->CloseScreen(); }, true, true, FText::GetEmpty()}}, false);
	}
	TSharedPtr<SButton> File;
	TSharedRef<SVerticalBox> Box = Column();
	const kc::GameState& S = Hud->Game()->GetState();
	AddRow(Box, MakeEyebrow(FText::FromString(FString::Printf(TEXT("Incident report  /  %s  /  %s"), *ToFString(M->Id), *C->ClockText()))), Space1);
	AddRow(Box, MakeHeading(FText::FromString(ToFString(M->Title))), 4.0f);
	AddRow(Box, MakeBody(LOCTEXT("ReportHelp", "Facts, not opinions. Write what you saw, heard and checked, and when. The report is scored when you file it, and not before."), 13, TextMuted), Space4);
	// The notebook beside the form: what the fieldwork found, in the order it was done.
	TArray<FString> Notes;
	for (const kc::FieldStep& Step : S.Campaign.Steps)
	{
		if (const kc::ActionDef* A = M->FindAction(Step.ActionId))
		{
			const FString Title = A->FindingTitle.empty() ? FString() : TEXT(": ") + ToFString(A->FindingTitle);
			Notes.Add(FString::Printf(TEXT("%s  %s%s"), *ToFString(kc::ClockText(Step.Minute)), *ToFString(A->Label), *Title));
		}
	}
	if (Notes.IsEmpty()) Notes.Add(TEXT("Nothing done yet. Fieldwork comes first: go and look."));
	AddBullets(Box, LOCTEXT("ReportNotes", "Your notebook"), Notes, TextMuted);
	// Required steps still open (the form will not file without them).
	TArray<FString> Missing;
	for (const kc::ActionDef& A : M->Actions)
	{
		if (A.bRequired && !C->IsDone(ToFString(A.Id))) Missing.Add(FString::Printf(TEXT("%s (%s)"), *ToFString(A.Label), *UKcCampaignSubsystem::LocationName(ToFString(A.At))));
	}
	AddBullets(Box, LOCTEXT("ReportMissing", "Still to do before you can file"), Missing, Warning);
	std::string Section = "\x01";
	TWeakObjectPtr<UKcCampaignSubsystem> Weak(C);
	for (const kc::QuestionDef* Q : C->ReportQuestions())
	{
		if (Q->Section != Section)
		{
			Section = Q->Section;
			AddRow(Box, MakeHeading(FText::FromString(SectionTitle(Section)), 17), Space2);
		}
		const FString Id = ToFString(Q->Id);
		AddRow(Box, WrappedLine(ToFString(Q->Prompt), 15, TextPrimary), Space2);
		AddRow(Box, AnswerEditor(C, *Q, [Weak, Id]() -> TArray<FString>& { return Weak->Draft().FindOrAdd(Id); }), Space4);
	}
	TSharedRef<FText> Status = MakeShared<FText>();
	TSharedRef<SVerticalBox> Root = Column();
	Root->AddSlot().FillHeight(1.0f)[Scrolling(Box)];
	Root->AddSlot().AutoHeight().Padding(0.0f, Space3, 0.0f, 0.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock).AutoWrapText(true).Font(FontW(13, EWeight::Medium)).ColorAndOpacity(Warning).Text_Lambda([Status]() { return *Status; })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(Space2, 0.0f, Space2, 0.0f)
		[
			MakeButton(LOCTEXT("ReportLater", "Keep the draft and close"), Click([Hud]() { Hud->CloseScreen(); }))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			MakeButton(LOCTEXT("ReportFile", "File the report"), Click([Hud, Where, Status]()
			{
				const EKcCaseResult R = Hud->FileReport(Where);
				if (R != EKcCaseResult::Ok) *Status = FText::FromString(UKcCampaignSubsystem::ResultText(R));
			}), &File, true, true)
		]
	];
	return {MakeModalFrame(Root, 1000.0f), File};
}

FKcScreenParts Debrief(AKcHUD* Hud, const kc::MissionDebrief& D, TFunction<void()> OnClose)
{
	UKcCampaignSubsystem* C = CampaignOf(Hud);
	TSharedPtr<SButton> Close;
	TSharedRef<SVerticalBox> Box = Column();
	const FString Kind = D.bWhatIf ? TEXT("What-if debrief") : TEXT("Debrief");
	AddRow(Box, MakeEyebrow(FText::FromString(FString::Printf(TEXT("%s  /  %s"), *Kind, *ToFString(D.MissionId)))), Space1);
	AddRow(Box, MakeHeading(FText::FromString(ToFString(D.Title))), 4.0f);
	FString Head = FString::Printf(TEXT("Score %d of 100.  %s"), D.Score, D.bPassed ? TEXT("Passed.") : TEXT("Not passed."));
	if (D.bCriticalBreach) Head += TEXT("  A serious policy or legal breach means this case cannot pass.");
	if (D.bWhatIf && D.PreviousBest >= 0) Head += FString::Printf(TEXT("  Your best real attempt: %d."), D.PreviousBest);
	AddRow(Box, WrappedLine(Head, 16, D.bPassed ? Good : Bad), Space4);
	// The seven rubric dimensions.
	TSharedRef<SVerticalBox> Dims = Column();
	for (int32 i = 0; i < kc::RubricCount; ++i)
	{
		const int32 V = D.DimensionScores[i];
		if (V < 0) continue;
		Dims->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(180.0f)[MakeBody(FText::FromString(ToFString(kc::ToString(static_cast<kc::ERubric>(i)))), 14, TextMuted)]]
			+ SHorizontalBox::Slot().AutoWidth()[MakeBody(FText::FromString(ScoreWord(V)), 14, V >= 60 ? Good : (V >= 40 ? Warning : Bad))]
		];
	}
	AddRow(Box, SNew(SBorder).BorderImage(CardBrush()).Padding(Space4)[Dims], Space4);
	AddRow(Box, MakeHeading(LOCTEXT("WhatHappened", "What really happened"), 16), 4.0f);
	AddRow(Box, WrappedLine(ToFString(D.WhatHappened), 14, TextPrimary), Space4);
	AddBullets(Box, LOCTEXT("WellDone", "Standards you met"), ToLines(D.WellDone), Good);
	// Every criterion with its feedback: the lesson, line by line.
	AddRow(Box, MakeHeading(LOCTEXT("Criteria", "How your work was judged"), 16), 4.0f);
	for (const kc::MissionCriterion& Cr : D.Criteria)
	{
		const FLinearColor Col = Cr.bCriticalFailed ? Bad : (Cr.Score >= 80 ? Good : (Cr.Score >= 50 ? Warning : Bad));
		AddRow(Box, WrappedLine(FString::Printf(TEXT("%s  (%s, weight %d):  %d"), *ToFString(Cr.Label), *ToFString(kc::ToString(Cr.Dimension)), Cr.Weight, Cr.Score), 14, Col), 1.0f);
		for (const std::string& F : Cr.Feedback) AddRow(Box, WrappedLine(TEXT("    ") + ToFString(F), 13, TextMuted), 1.0f);
		if (Cr.bAuthorityExceeded) AddRow(Box, WrappedLine(TEXT("    Above your authority: this needed escalating."), 13, Warning), 1.0f);
		AddGap(Box, 4.0f);
	}
	AddGap(Box, 6.0f);
	AddBullets(Box, LOCTEXT("BestPractice", "Best practice, in order"), ToLines(D.BestPractice), TextPrimary, true);
	if (!D.Why.empty())
	{
		AddRow(Box, MakeHeading(LOCTEXT("Why", "Why it matters"), 16), 4.0f);
		AddRow(Box, WrappedLine(ToFString(D.Why), 14, TextPrimary), Space4);
	}
	AddBullets(Box, LOCTEXT("Mistakes", "Common mistakes"), ToLines(D.CommonMistakes), TextMuted);
	AddBullets(Box, LOCTEXT("Timeline", "Your timeline"), ToLines(D.Timeline), TextMuted);
	TArray<FString> Cards;
	for (const std::string& Id : D.CardsEarned)
	{
		const kc::ProcedureCard* Card = C ? kc::FindCard(C->GetCards(), Id) : nullptr;
		Cards.Add(Card ? ToFString(Card->Title) : ToFString(Id));
	}
	AddBullets(Box, LOCTEXT("Cards", "New Field Manual cards"), Cards, Accent);
	AddBullets(Box, LOCTEXT("Fallout", "Risk your weaker choices left behind"), ToLines(D.Fallout), Warning);
	if (!D.bWhatIf)
	{
		if (D.bPromoted) AddRow(Box, WrappedLine(FString::Printf(TEXT("Promoted: you are now %s."), *ToFString(kc::LevelDef(D.Level).Title)), 16, Good), Space3);
		else AddBullets(Box, LOCTEXT("Gaps", "Before your next promotion"), ToLines(D.PromotionGaps), TextMuted);
	}
	if (!D.NewMessages.empty()) AddRow(Box, MakeBody(FText::Format(LOCTEXT("NewMessages", "{0} new message(s) in your inbox."), static_cast<int32>(D.NewMessages.size())), 14, Accent), Space2);
	if (D.XpAwarded > 0) AddRow(Box, MakeBody(FText::Format(LOCTEXT("Xp", "{0} XP. XP is feedback: promotion comes from evidence of competence and passed cases."), D.XpAwarded), 12, TextMuted), Space2);
	const FString MissionId = ToFString(D.MissionId);
	TSharedRef<SVerticalBox> Root = Column();
	Root->AddSlot().FillHeight(1.0f)[Scrolling(Box)];
	Root->AddSlot().AutoHeight().Padding(0.0f, Space3, 0.0f, 0.0f).HAlign(HAlign_Right)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, Space2, 0.0f)
		[
			MakeButton(LOCTEXT("WhatIf", "Try another approach (what-if)"), Click([Hud, MissionId]()
			{
				UKcCampaignSubsystem* Camp = Hud->Campaign();
				const EKcCaseResult R = Camp ? Camp->StartWhatIf(MissionId) : EKcCaseResult::NotActive;
				Hud->CloseScreen();
				if (R != EKcCaseResult::Ok) Hud->Notice(UKcCampaignSubsystem::ResultText(R));
			}))
		]
		+ SHorizontalBox::Slot().AutoWidth()[MakeButton(LOCTEXT("DebriefClose", "Close"), Click(OnClose), &Close, true, true)]
	];
	return {MakeModalFrame(Root, 1000.0f), Close};
}

FKcScreenParts Journal(AKcHUD* Hud)
{
	UKcCampaignSubsystem* C = CampaignOf(Hud);
	const UKcGameSubsystem* G = Hud->Game();
	const kc::GameState& S = G->GetState();
	const kc::MissionDef* M = C ? C->Active() : nullptr;
	TSharedPtr<SButton> Close;
	TSharedRef<SVerticalBox> Box = Column();
	AddRow(Box, MakeEyebrow(FText::FromString(FString::Printf(TEXT("Notebook  /  %s, level %d"), *G->GetLevelTitle(), G->GetLevel()))), Space1);
	if (!M)
	{
		AddRow(Box, MakeHeading(LOCTEXT("NoCase", "No case open")), 4.0f);
		AddRow(Box, MakeBody(LOCTEXT("NoCaseHelp", "Sign on at the terminal in the LP office (B1) to start your shift."), 14, TextMuted), Space4);
	}
	else
	{
		AddRow(Box, MakeHeading(FText::FromString(ToFString(M->Title) + (C->IsWhatIf() ? TEXT("  (what-if)") : TEXT("")))), 2.0f);
		AddRow(Box, MakeBody(FText::FromString(FString::Printf(TEXT("Case clock %s.  The report is filed at the %s."), *C->ClockText(), *UKcCampaignSubsystem::LocationName(ToFString(M->ReportAt)))), 13, TextMuted), Space3);
		AddBullets(Box, LOCTEXT("Briefing", "Briefing"), ToLines(kc::BriefingFor(S, *M)), TextPrimary);
		// Findings, in the order the work was done.
		AddRow(Box, MakeHeading(LOCTEXT("Findings", "Findings"), 16), 4.0f);
		if (S.Campaign.Steps.empty()) AddRow(Box, MakeBody(LOCTEXT("NoFindings", "Nothing yet."), 14, TextMuted), 4.0f);
		for (const kc::FieldStep& Step : S.Campaign.Steps)
		{
			const kc::ActionDef* A = M->FindAction(Step.ActionId);
			if (!A) continue;
			AddRow(Box, WrappedLine(FString::Printf(TEXT("%s  %s  (%s)"), *ToFString(kc::ClockText(Step.Minute)), *ToFString(A->Label), *UKcCampaignSubsystem::LocationName(ToFString(A->At))), 14, TextPrimary), 1.0f);
			const FString Finding = C->FindingFor(*A);
			if (!Finding.IsEmpty()) AddRow(Box, WrappedLine(TEXT("    ") + Finding, 13, TextMuted), 5.0f);
		}
		AddGap(Box, 6.0f);
		// What the player said face to face (the choice, never its score).
		TArray<FString> Said;
		for (const kc::MissionAnswer& Ans : S.Campaign.FieldAnswers)
		{
			const kc::QuestionDef* Q = M->FindQuestion(Ans.QuestionId);
			if (!Q) continue;
			TArray<FString> Labels;
			for (const std::string& Id : Ans.OptionIds)
			{
				for (const kc::OptionDef& O : Q->Options)
				{
					if (O.Id == Id) Labels.Add(ToFString(O.Label));
				}
			}
			Said.Add(FString::Printf(TEXT("To %s: %s"), *C->PersonName(Q->Person), *FString::Join(Labels, TEXT("; "))));
		}
		AddBullets(Box, LOCTEXT("Said", "What you said"), Said, TextMuted);
		TArray<FString> Custody;
		for (const kc::CustodyEntry& E : S.Campaign.Custody) Custody.Add(FString::Printf(TEXT("%s  %s"), *ToFString(kc::ClockText(E.Minute)), *ToFString(E.Text)));
		AddBullets(Box, LOCTEXT("CustodyLog", "Chain of custody"), Custody, TextMuted);
		if (G->GetSettings().bHints)
		{
			TArray<FString> Places;
			for (const FString& P : C->PlacesWithWork()) Places.Add(UKcCampaignSubsystem::LocationName(P));
			AddBullets(Box, LOCTEXT("Open", "Hint: places with work left"), Places, Accent);
		}
	}
	// The career and the people, briefly (the terminal has the full career page).
	const kc::PromotionCheck P = S.Career.CheckPromotion();
	AddBullets(Box, LOCTEXT("NextPromotion", "Next promotion"), P.bEligible ? TArray<FString>{TEXT("You meet every requirement for your level.")} : ToLines(P.UnmetGates), TextMuted);
	TArray<FString> People;
	for (const kc::Relation& R : S.World.Relations)
	{
		const kc::CharacterDef* Who = C ? C->FindPerson(ToFString(R.CharacterId)) : nullptr;
		if (Who && Who->bStaff) People.Add(FString::Printf(TEXT("%s, %s: %s"), *ToFString(Who->Name), *ToFString(Who->Role), *ToFString(kc::RelationLabel(R.Value))));
	}
	AddBullets(Box, LOCTEXT("People", "Colleagues"), People, TextMuted);
	TArray<FString> History = ToLines(S.LearningHistory);
	Algo::Reverse(History);
	if (History.Num() > 6) History.SetNum(6);
	AddBullets(Box, LOCTEXT("Learned", "What you have learned"), History, TextMuted);
	TSharedRef<SVerticalBox> Root = Column();
	Root->AddSlot().FillHeight(1.0f)[Scrolling(Box)];
	Root->AddSlot().AutoHeight().Padding(0.0f, Space3, 0.0f, 0.0f).HAlign(HAlign_Right)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, Space2, 0.0f)
		[
			MakeButton(LOCTEXT("JManual", "Field Manual"), Click([Hud]() { Hud->CloseScreen(); Hud->OpenFieldManual([Hud]() { Hud->CloseScreen(); Hud->OpenJournal(); }); }))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, Space2, 0.0f)
		[
			MakeButton(LOCTEXT("JCareer", "Career"), Click([Hud]() { Hud->CloseScreen(); Hud->OpenCareer([Hud]() { Hud->CloseScreen(); Hud->OpenJournal(); }); }))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, Space2, 0.0f)
		[
			MakeButton(LOCTEXT("JInbox", "Inbox"), Click([Hud]() { Hud->CloseScreen(); Hud->OpenInbox(); }))
		]
		+ SHorizontalBox::Slot().AutoWidth()[MakeButton(LOCTEXT("CloseJournal", "Close"), Click([Hud]() { Hud->CloseScreen(); }), &Close, true, true)]
	];
	return {MakeModalFrame(Root, 900.0f), Close};
}

FKcScreenParts FieldManual(AKcHUD* Hud, TFunction<void()> OnBack)
{
	UKcCampaignSubsystem* C = CampaignOf(Hud);
	const kc::GameState& S = Hud->Game()->GetState();
	TSharedPtr<SButton> Back;
	TSharedRef<SVerticalBox> Box = Column();
	AddRow(Box, MakeEyebrow(LOCTEXT("FmEyebrow", "Veldane Loss Prevention")), Space1);
	AddRow(Box, MakeHeading(FText::Format(LOCTEXT("FmTitle", "Field Manual ({0} of {1} cards)"), static_cast<int32>(S.FieldManual.size()), C ? static_cast<int32>(C->GetCards().size()) : 0)), 4.0f);
	AddRow(Box, MakeBody(LOCTEXT("FmHelp", "Each case you close adds the procedure it taught. These are house procedures; local law and counsel decide where they differ."), 13, TextMuted), Space4);
	if (S.FieldManual.empty()) AddRow(Box, MakeBody(LOCTEXT("FmEmpty", "No cards yet. Close your first case."), 14, TextMuted), Space2);
	for (const std::string& Id : S.FieldManual)
	{
		const kc::ProcedureCard* Card = C ? kc::FindCard(C->GetCards(), Id) : nullptr;
		if (!Card) continue;
		TSharedRef<SVerticalBox> CardBox = Column();
		AddRow(CardBox, MakeHeading(FText::FromString(ToFString(Card->Title)), 17), Space2);
		for (size_t i = 0; i < Card->Steps.size(); ++i) AddRow(CardBox, WrappedLine(FString::Printf(TEXT("%d.  %s"), static_cast<int32>(i + 1), *ToFString(Card->Steps[i])), 14, TextPrimary, 760.0f), 2.0f);
		if (!Card->Why.empty()) AddRow(CardBox, WrappedLine(TEXT("Why: ") + ToFString(Card->Why), 13, TextMuted, 760.0f), 4.0f);
		for (const std::string& Pit : Card->Pitfalls) AddRow(CardBox, WrappedLine(TEXT("Pitfall: ") + ToFString(Pit), 13, Warning, 760.0f), 2.0f);
		AddRow(Box, SNew(SBorder).BorderImage(CardBrush()).Padding(Space4)[CardBox], Space3);
	}
	// The glossary terms for the cases met so far.
	TArray<FString> Terms;
	if (C)
	{
		for (const kc::GlossaryTerm& T : C->GetGlossary())
		{
			bool bMet = T.Missions.empty();
			for (const std::string& Mid : T.Missions)
			{
				bMet = bMet || std::find(S.Career.AttemptedMissions.begin(), S.Career.AttemptedMissions.end(), Mid) != S.Career.AttemptedMissions.end();
			}
			if (bMet) Terms.Add(FString::Printf(TEXT("%s: %s"), *ToFString(T.Term), *ToFString(T.Definition)));
		}
	}
	AddBullets(Box, LOCTEXT("Glossary", "Glossary"), Terms, TextMuted);
	TSharedRef<SVerticalBox> Root = Column();
	Root->AddSlot().FillHeight(1.0f)[Scrolling(Box)];
	Root->AddSlot().AutoHeight().Padding(0.0f, Space3, 0.0f, 0.0f).HAlign(HAlign_Right)[MakeButton(LOCTEXT("FmBack", "Back"), Click(OnBack), &Back, true, true)];
	return {MakeModalFrame(Root, 900.0f), Back};
}

FKcScreenParts Career(AKcHUD* Hud, TFunction<void()> OnBack)
{
	const kc::GameState& S = Hud->Game()->GetState();
	const kc::CareerLevelDef& L = kc::LevelDef(S.Career.Level);
	TSharedPtr<SButton> Back;
	TSharedRef<SVerticalBox> Box = Column();
	AddRow(Box, MakeEyebrow(FText::FromString(FString::Printf(TEXT("Career  /  level %d of %d"), S.Career.Level, kc::TopLevel))), Space1);
	AddRow(Box, MakeHeading(FText::FromString(ToFString(L.Title))), 4.0f);
	AddRow(Box, WrappedLine(ToFString(L.Responsibilities), 14, TextPrimary), Space2);
	AddRow(Box, MakeBody(FText::Format(LOCTEXT("Authority", "Spend you can approve without escalating: {0}"), Money(L.DecisionAuthority)), 14, TextMuted), Space2);
	AddRow(Box, MakeBody(FText::FromString(TEXT("Background: ") + ToFString(kc::Background(S.Career.Origin).Title)), 14, TextMuted), Space4);
	const kc::PromotionCheck P = S.Career.CheckPromotion();
	AddBullets(Box, LOCTEXT("CareerNext", "Next promotion"),
		P.bEligible ? TArray<FString>{TEXT("You meet every requirement. The promotion review follows your next filed report.")} : ToLines(P.UnmetGates), TextPrimary);
	AddRow(Box, MakeHeading(LOCTEXT("Competencies", "Competencies (from your assessed work)"), 16), 4.0f);
	for (int32 i = 0; i < kc::CompetencyCount; ++i)
	{
		const kc::CompetencyRecord& R = S.Career.Competencies[static_cast<size_t>(i)];
		AddRow(Box, SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(300.0f)[MakeBody(FText::FromString(ToFString(kc::ToString(static_cast<kc::ECompetency>(i)))), 14, TextMuted)]]
			+ SHorizontalBox::Slot().AutoWidth()[MakeBody(FText::FromString(FString::Printf(TEXT("%d   (%d assessed)"), R.Score, R.Assessments)), 14, R.Assessments > 0 ? TextPrimary : TextMuted)], 2.0f);
	}
	AddGap(Box, 8.0f);
	AddRow(Box, MakeBody(FText::Format(LOCTEXT("Passed", "Cases passed: {0}.  XP: {1} (feedback only: XP never decides a promotion)."), static_cast<int32>(S.Career.PassedMissions.size()), S.Career.Xp), 13, TextMuted), Space2);
	TSharedRef<SVerticalBox> Root = Column();
	Root->AddSlot().FillHeight(1.0f)[Scrolling(Box)];
	Root->AddSlot().AutoHeight().Padding(0.0f, Space3, 0.0f, 0.0f).HAlign(HAlign_Right)[MakeButton(LOCTEXT("CareerBack", "Back"), Click(OnBack), &Back, true, true)];
	return {MakeModalFrame(Root, 860.0f), Back};
}

FKcScreenParts Lift(AKcHUD* Hud, int32 FromFloor)
{
	TArray<FKcChoice> Items;
	for (int32 F = AKcHotel::TopFloor; F >= AKcHotel::LowestFloor; --F)
	{
		if (!AKcHotel::IsFittedFloor(F))
		{
			// Guest floors 1-13: one disabled row.
			if (F == 13) Items.Add({LOCTEXT("LiftGuestFloors", "1 - 13  Guest floors"), nullptr, false, false, LOCTEXT("LiftGuestNote", "No calls on these floors tonight.")});
			continue;
		}
		const bool bHere = F == FromFloor;
		const FString Name = F == 14 ? TEXT("14  Guest rooms 1401-1420") : (F == 0 ? TEXT("G  Lobby, front desk, lobby bar") : TEXT("B1  LP office, CCTV room, back of house"));
		Items.Add({FText::FromString(Name + (bHere ? TEXT("  (you are here)") : TEXT(""))), [Hud, F]() { Hud->RideLift(F); }, false, !bHere, FText::GetEmpty()});
	}
	Items.Add({LOCTEXT("LiftCancel", "Stay on this floor"), [Hud]() { Hud->CloseScreen(); }, false, true, FText::GetEmpty()});
	return Choices(FText::FromString(FString::Printf(TEXT("Lift  /  floor %s"), *AKcHotel::FloorLabel(FromFloor))), LOCTEXT("LiftTitle", "Choose a floor"),
		LOCTEXT("LiftBody", "Your staff card opens every floor. In a fire alarm the lifts are not used: take the stairs."), Items, false);
}

TSharedRef<SWidget> CeremonyOverlay(const FString& Title, const FString& Subtitle)
{
	const FLinearColor Bar(0.0f, 0.0f, 0.0f, 0.92f);
	return SNew(SBox).Visibility(EVisibility::HitTestInvisible)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(110.0f)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Bar)]]
		+ SVerticalBox::Slot().FillHeight(1.0f).VAlign(VAlign_Bottom).HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 40.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Text(LOCTEXT("CeremonyEyebrow", "VELDANE GRAND HARBOURFRONT")).Font(FontW(13, EWeight::SemiBold)).ColorAndOpacity(Accent)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, Space2)[SNew(STextBlock).Text(FText::FromString(Title)).Font(FontW(44, EWeight::SemiBold)).ColorAndOpacity(TextPrimary)]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Text(FText::FromString(Subtitle)).Font(FontW(17, EWeight::Regular)).ColorAndOpacity(TextMuted)]
		]
		+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(110.0f)[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Bar)]]
	];
}
} // namespace KcScreens

#undef LOCTEXT_NAMESPACE
