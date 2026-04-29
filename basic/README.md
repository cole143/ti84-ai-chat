# TI-BASIC Programs for FIN 30103 Equities Exam

Seven standalone calculator programs that run **without the laptop**. Each
has a 3-mode menu:

1. **CALCULATE** — prompts for inputs, returns the answer.
2. **HOW TO USE** — names the formula, explains when to apply it, calls out traps.
3. **WORKED EXAMPLE** — fully solved practice-exam-style problem with step-by-step.

## How to type these into the calculator

1. On the calc: `[PRGM]` -> `NEW` -> `Create New` -> name (max 8 chars uppercase) -> `[ENTER]`.
2. Type each line from the `.txt` source. Newline = `[ENTER]` = new program line.
3. Get tokens like `Disp`, `Input`, `Menu(`, `Lbl`, `Goto`, `Pause`, `Stop`,
   `If`, `Then`, `End`, `ClrHome` from their menu locations:
   - `Disp`, `Input`, `Pause`, `ClrHome`: `[PRGM]` while editing -> `I/O` tab
   - `Menu(`, `Lbl`, `Goto`, `If`, `Then`, `End`, `Stop`: `[PRGM]` while editing -> `CTL` tab
   - `Then` and `End` are tokens, not strings — pick them from the CTL menu.
   - The store arrow `->` is the `[STO>]` key on the calculator.
4. Save by pressing `[2nd]` `[QUIT]`.
5. Run from `[PRGM] > EXEC > prgmNAME`.

## How to skip step 1 (optional, faster)

If you have TI Connect CE: `File > New > TI-Basic CE Program`, paste the source,
save, send to calc. **Test this on one program first** before committing to it
for all seven — TI Connect's BASIC editor is finicky with token translation.

## Programs

| Name    | Purpose                          | Key formula                              |
|---------|----------------------------------|------------------------------------------|
| MARGIN  | Margin call price (long)         | P0*(1-IM)/(1-MM)                         |
| MRETURN | Return on a margined long        | (P_end - P0 + D)/(IM*P0)                 |
| LEVRAT  | Leverage ratio                   | 1/IM                                     |
| DDM     | Single-stage Gordon DDM          | D1/(k-g)                                 |
| MDDM    | Two-stage DDM                    | D1/(1+k)+D2/(1+k)^2+TV2/(1+k)^2          |
| JPE     | Justified P/E (also g=b*ROE)     | (1-b)/(k-g); g=b*ROE                     |
| PREF    | Preferred stock value            | D/k                                      |

## Verification table (run CALCULATE with these inputs)

| Program | Inputs                       | Expected output |
|---------|------------------------------|-----------------|
| MARGIN  | P=26, IM=0.5, MM=0.25        | 17.333          |
| MRETURN | P=32, E=34, IM=0.4, D=0      | 0.15625         |
| LEVRAT  | IM=0.25                      | 4               |
| DDM     | D=2, K=0.10, G=0.04          | 33.333          |
| MDDM    | A=1.25, B=1.56, G=0.05, K=0.11 | 24.55         |
| JPE     | mode 1, B=0.40, K=0.10, G=0.04 | 10.0          |
| PREF    | D=7, K=0.0775                | 90.322          |

If any value disagrees by more than 0.05, re-check the BASIC source against
the formula or against `host/formula_calcs.py` (the Python source of truth).
