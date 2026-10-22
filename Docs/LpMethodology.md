# LP methodology

Every Loss Prevention figure the game computes, scores or shows comes from `Source/KeycardSim/Public/KcLpMath.h` (namespace `kc::lp`). Tools, mission scoring and UI call these functions; nothing re-implements them in Blueprints or Slate. Tests: `Tests/Core/TestLpMath.cpp` (the worked examples below are the test values).

Ground rules
- Money is plain currency units (the game never names a real currency).
- Ordinal ratings (1 to 5 likelihood, A to E audit grades, relationship levels) are never money and are never multiplied into money.
- Annualised loss is an average. It is never presented as a worst case.
- Invalid inputs (negative counts, zero denominators, NaN or infinity) return `bValid = false`. The UI shows "check the inputs", never a silent zero.
- These are training-style simplifications of widely published practice. Real policies, brand standards and local law decide the real numbers.

## Inventory shrink
`ShrinkPercent(Book, Counted) = (Book - Counted) / Book x 100`
Used for minibar, F&B stores, linen, retail outlets, receiving dock. Positive is loss, negative is an overage (which is also a finding: it often means receiving or posting errors).
Example: book 12,000, counted 11,520: shrink 4.0%.

## Food and beverage cost variance
`CostVariancePoints(Actual, Ideal, Sales) = (Actual - Ideal) / Sales x 100` percentage points.
Ideal cost is what the sales should have used (recipes and pour standards); actual is opening stock + purchases - closing stock.
Example (banquet liquor): sales 40,000, ideal 8,000 (20%), actual 9,400 (23.5%): variance 3.5 points.

## Loss rate
`LossRatePer1000RoomNights(Incidents or loss value, Occupied room nights) = value / room nights x 1,000`
Normalises theft, injury or claim counts for occupancy so a busy month is not mistaken for a worse one.
Example: 18 room-theft reports over 15,000 occupied room nights: 1.2 per 1,000.

## Claim reserve and incurred
`ClaimReserve = max(0, Medical + Indemnity + Expense - PaidToDate)`; `ClaimIncurred = PaidToDate + Reserve`.
The reserve is the claims administrator's estimate of what is still to pay. LP supplies the facts (photos, statements, footage, timeline) that make the estimate credible; LP never admits liability.
Example: medical 6,500, indemnity 12,000, expense 4,000, paid 2,500: reserve 20,000, incurred 22,500.

## Cost of an incident
`IncidentCost = Uninsured + ResponseHours x HourlyRate + Repairs + GuestRecovery + min(InsuredLoss, Deductible)`
`InsurerRecovery = max(0, InsuredLoss - Deductible)`
The hotel always keeps the deductible part of an insured loss. Guest recovery covers refunds, comps and goodwill.
Example: uninsured 300, 6 h at 35, repairs 450, guest recovery 600, insured loss 8,000 with a 5,000 deductible: cost 6,560, insurer pays 3,000.

## Patrol compliance
`PatrolCompliancePercent(ScannedOnTime, Scheduled) = scanned / scheduled x 100`
Only checkpoints scanned inside their window count. Example: 57 of 60: 95%.

## LP audit score
`Score = sum(weight x points) / sum(weight x max points) x 100`, not-applicable items left out.
Pass = score >= pass mark AND no critical item short of full marks. A failed critical item (for example a propped fire door or an unaccounted master key) fails the audit whatever the score, as brand audits do. Each finding should carry a corrective action, an owner and a date.
Example: items (2/2 w1), (1/2 w2), (3/3 w1, critical), one N/A: 7/9 = 77.8%, below an 80% pass mark.

## Staffing and coverage
- `PostHoursPerWeek(HoursPerDay, DaysPerWeek, Posts) = hours x days x posts`
- `FtesRequired(PostHours, ProductiveHoursPerFteYear) = PostHours x 52 / productive hours`
- `StaffingCoveragePercent(Scheduled, Required) = scheduled / required x 100`
Productive hours are what an officer actually works after leave, training and sickness (about 1,800 a year is a common planning figure). Example: two 24/7 posts = 336 h/week = 9.7 FTE at 1,800 h; 302.4 scheduled hours is 90% coverage.

## Re-key cost
`RekeyCost = Doors x (CostPerDoor + LabourMinutesPerDoor / 60 x HourlyRate) + KeysReissued x CostPerKey`
Electronic locks: cost per door is the re-code and audit time; mechanical: cylinders or cores. The decision in missions is cost against the risk of the lost key (annualised loss below), never cost alone.
Example: 120 doors at 18 plus 10 min at 42/h, 40 keys at 3.50: 3,140.

## Annualised loss
`AnnualisedLoss(EventsPerYear, AverageCost) = events x average cost`. Example: 4 a year at 2,500: 10,000 a year.

## Scoring typed figures
`ScoreByCloseness(Answer, Truth, Tolerance)` compares relative error: within tolerance 100, within 3x 70, within 10x 35, otherwise 0. A zero truth uses absolute error. Typed ratings use the same rule.
