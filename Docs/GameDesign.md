# Game design

## One paragraph
Keycard is an offline, single-player, first-person career simulator about hotel Loss Prevention. The player joins LP at the Veldane Grand Harbourfront (600 rooms, full service, fictional city of Port Avener), flagship of Veldane Hotels & Resorts, and rises from LP Officer on nights to Vice President, Global Safety & Security, by doing real LP work. It is a learning game first: every mission is a realistic scenario that teaches a real procedure, and the debrief explains best practice and why.

## Core loop: learning by scenarios
Every mission:
1. **Starts in the world**: a radio call, a guest at the desk, a fire panel alarm, an HR email, a claim letter, a GM's request. Never a menu.
2. **Is worked on foot**: walk to the scene, look, talk to people face to face, pull CCTV, interrogate a lock, check logs, photograph and bag evidence, write it up. A core test proves every case can be worked on foot (each case uses at least two places and one face-to-face conversation; every evidence item has a place).
3. **Teaches one or two named skills** (`learningObjectives` in the mission data): scene preservation, chain of custody, de-escalation, lock audit trail, report writing, fire alarm order of actions, non-leading questions.
4. **Has trade-offs, not quiz answers**: guest satisfaction vs safety, speed vs evidence quality, cost vs coverage, loyalty to staff vs policy. Sloppy choices are allowed and have consequences.
5. **Scores against a hidden rubric**, shown only after submission: procedure in the right order, evidence quality and chain of custody, policy and legal compliance, guest and staff care, the written report, communication with the right people, cost.
6. **Ends with a mini lesson**: what happened, what a strong LP professional would have done and in what order, why (the risk, the liability, the person), common mistakes, what the player did well, and a **procedure card** added to the Field Manual.
7. **Is replayable from the debrief** ("what if") to try the other approach.

Difficulty grows with rank: officers follow procedure, managers write and enforce it, directors set programmes, budgets and culture, executives answer to the board, insurers and the public.

Policy in every mission: Veldane LP is **observe, report and de-escalate**. Physical intervention, searches of people and detention follow policy and local law only; breaking policy is scored and has consequences. Scenarios are law-neutral ("local law", "the fire authority", "counsel"); the credits say this is training-style fiction, not legal advice. Sensitive topics (guest death, missing child, human trafficking awareness, active threat) are handled with care: procedure, dignity and communication, no graphic content, no player use of weapons.

## Rubric dimensions (hidden until submission)
| Dimension | What it checks |
|---|---|
| Procedure | Steps done, in the right order (for example: preserve the scene before anyone enters) |
| Evidence | Quality, completeness, chain of custody unbroken |
| Compliance | Policy and local law (no unlawful search, detention, admission of liability) |
| Care | Guest and staff welfare, dignity, de-escalation |
| Report | Who, what, when, where, how; facts not opinions |
| Communication | The right people, at the right time, with the right information |
| Cost | Spend proportionate to risk, within decision authority |

## Campaign map (ids: Docs/CareerModel.md)
| Level | Missions (working titles) | Topics |
|---|---|---|
| 1 LP Officer | M001 Night Shift: Room 1412 (laptop theft claim, lock audit, CCTV, report); M002 Locked Out (identity check before room entry); M003 The 2 a.m. Noise Complaint (escalating, de-escalation) | Incident response, report writing |
| 2 Senior LP Officer | M004 Lobby Intoxication; M005 Code Blue (medical emergency, AED, first aid); M006 Unattended Bag; M007 Footage Before It Overwrites (retention, export, chain of custody) | CCTV control room, radio lead |
| 3 LP Supervisor | M008 Lost Master Key (re-key cost vs risk); M009 The Propped Door (back of house); M010 Bell Stand Luggage Theft; M011 Keycard Encoding Audit | Key control, first-line investigation |
| 4 Assistant LP Manager | M012 Fire Alarm, Floor 9 (order of actions); M013 Sprinkler Impairment and Fire Watch; M014 Slip and Fall in the Lobby (photos, statements, no admission, insurer report); M015 Hot Work Permit | Fire and life safety, claims |
| 5 LP Manager | M016 Harassment Complaint with HR; M017 Banquet Liquor Shrink; M018 Brand LP Audit and Corrective Actions; M019 Contract vs In-house Officers (budget) | Investigations, audits, budget |
| 6 Director of LP | M020 Missing Child (code procedure, door control, search); M021 Guest Death in a Room (scene, dignity, police, next of kin through the right people); M022 Fire Marshal Inspection; crisis C001 Power Outage at Full Occupancy | Executive committee, crisis lead |
| 7 Cluster Director | M023 Two Hotels, One Standard; M024 Former Employee Access Not Removed; M025 Chargeback Fraud Ring; M026 Supervisor Protecting a Friend; crisis C002 Bomb Threat Call | Standards across sites |
| 8 Area Director | M027 Insurer Renewal Review; M028 Food Illness Claim with the Health Department; M029 Human Trafficking Awareness Programme; M030 Fraudulent Claim Spotted from Evidence; crisis C003 Major Fire on a Guest Floor | Insurer and audit relationships |
| 9 Regional Director | M031 Lock System Business Case; M032 CCTV Upgrade and Privacy Rules; M033 Viral Video of a Security Incident; M034 Retaliation Risk; crisis C004 Hurricane at the Cliffside Resort | Programmes, technology, legal |
| 10 VP | X001 capstone: a multi-property incident that becomes a board, insurer, media and family moment; the player presents the post-incident review and the global programme change to the board | Board |

Optional missions M035-M040 (valet and garage break-ins, skip-outs, minibar variance, front desk till shortage, refund and comp abuse, elevator entrapment, pool and spa safety, severe weather shelter, evacuation drill critique, police request for footage) and incident-triggered crises C005-C012 (protest at a VIP event, basement flood in the plant room, guest data breach, active threat lockdown, celebrity jewellery theft, convention food poisoning outbreak, kitchen fire suppression failure, trespasser in back of house that escalates) add evidence but are never required for promotion. Six missions may be inspired by real, publicly reported hotel incidents, fully fictionalised and labelled "inspired by real events".

## Systems carried over from Riskbound
- Walk-around casework, choices that come back later (seeded latent risk that surfaces as an incident, a lawsuit, a failed audit or a crisis; the report names the earlier decision), people who remember (about 15 colleagues, Hostile to Ally, never gating promotion), inbox with reply deadlines, stress and burnout, office politics (revenue and guest satisfaction vs safety and cost).
- A story underneath: an organised theft ring using a corrupt employee and cloned keycards across Veldane properties, uncovered clue by clue, with a mentor or GM who may be involved. Optional chapters, several endings.
- LP tools, one formula each (Docs/LpMethodology.md): incident report writer, CCTV review console (camera map, timeline, bookmarks, clip export with custody log), lock interrogation and key log viewer, patrol tour and daily activity report, fire panel and fire watch log, evidence locker, claims tracker, investigation case board, LP audit checklist, shrink and loss analytics, staffing and budget planner, crisis command log.
- Eight endings, performance review and certificate, what-if replays, Survival (busy night shift), Daily and Classroom modes with a local leaderboard, glossary, photo mode, mission mods.
- Living hotel (guests and staff, occupancy, day and night, weather, case markers, radio chatter); accessibility (text size, reduced motion, colour-safe palette, controller, invert look, upscaling, frame cap, vsync, hints, keyboard menus); adaptive sound; versioned checksummed local saves with backup and migration; optional AI colleagues (off by default, player's own key).

## People, faces and voices
Microsoft Rocketbox "_facial" avatars (MIT) dressed in fictional Veldane uniforms and badges made as textures; blinks, gaze, head turn, text-driven lip-sync stretched to each clip, expressions, gestures, walkers and seated meetings. Voices generated offline with Kokoro-82M (Apache 2.0), one voice or blend per character, clips found by a stable line key, played from the speaker's head about 8 LU above music. Not MetaHuman.
Cast (about 15, names checked before use in Phase 1): GM, Director of LP (mentor), HR director, chief engineer, executive housekeeper, front office manager, F&B director, rival supervisor, night auditor, contract guards, insurer's adjuster, police liaison, fire marshal.

## The hotel (Phase 1 builds the first four at final quality)
Lobby and front desk; LP office and CCTV control room; one guest floor (14) with rooms; back-of-house corridors and loading dock; housekeeping; kitchen; ballroom; pool and spa; parking garage; engineering and fire command centre; executive offices. Sister properties reuse the modular kit: Veldane Airport Port Avener (select service), Veldane Cliffside Resort (resort and spa), Veldane Convention Centre Hotel.

## Phases (stop for the owner after each)
- **Phase 0**: audit the PC and Riskbound, repo skeleton, reusable core and pipelines, CLAUDE.md, this file, LpMethodology, CareerModel, fictional-name check list. Core tests run.
- **Phase 1**: vertical slice, M001 "Night Shift: Room 1412" with lobby, LP office, CCTV room and floor 14 at final look, real faces and voices, first promotion ceremony; packaged self-test play and resume pass; owner playtests.
- **Phase 2**: tools and levels 1-3 (about 12 missions), inbox, relationships, Field Manual.
- **Phase 3**: levels 4-7, crises, consequences that come back, sister property, story chapters.
- **Phase 4**: levels 8-10, capstone, endings, modes, accessibility, sound and voice for every fixed line.
- **Phase 5**: full walkthrough, polish, installer, release 1.0.0.
