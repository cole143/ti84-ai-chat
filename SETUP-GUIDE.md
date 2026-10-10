# TI-84 AI Chat Bridge — Complete Setup Guide

Welcome! This guide walks you through every step needed to get your TI-84 Plus CE
talking to Claude AI. No prior programming experience is assumed. Each step is
numbered and self-contained — work through them in order.

**Total time:** About 25–30 minutes if everything goes smoothly.

---

## What You Need (Hardware)

Before you start, gather these items:

### 1. TI-84 Plus CE Calculator
The standard silver/color calculator sold for high school and college math. Make sure
it has working batteries.

### 2. Your Standard TI USB Cable
The cable that came in the box with your calculator (Mini-B to USB-A). This is the
**only cable you need**. It is used both for sending the program to the calculator
AND for running the chat bridge.

**If your PC only has USB-C ports:** You will also need a USB-C to USB-A adapter
(a small dongle). Plug the TI cable's USB-A end into the adapter, then plug the
adapter into your PC's USB-C port.

### 5. Windows 10 or Windows 11 PC
With an internet connection. You will need administrator access to install software.

### 6. Python 3.10 or Newer
If you are not sure whether Python is installed, skip to Part 1, Step 1.

---

## A Quick Word About What Happens

Here is the big picture so the steps make sense:

1. You will write and compile a small C program that runs on the calculator.
2. You will send that program to the calculator using TI's own software.
3. You will run a Python script on your PC that listens over USB.
4. When you type a question on the calculator and press ENTER, it gets sent to the PC.
5. The PC sends it to Claude AI, gets an answer, and sends it back to the calculator.
6. The answer appears on your calculator screen.

---

## Part 1: PC Setup (about 5 minutes)

### Step 1: Verify Python is Installed

1. Press the **Windows key** on your keyboard.
2. Type `cmd` and press **Enter**. A black Command Prompt window opens.
3. Type the following and press **Enter**:
   ```
   python --version
   ```
4. You should see something like `Python 3.11.4` or similar.
   - If you see a version number starting with 3.10 or higher: great, move on.
   - If you see `Python 2.7`: you need to install Python 3. Go to python.org,
     download the latest installer, run it, and **check the box that says
     "Add Python to PATH"** before clicking Install. Then close and reopen
     Command Prompt and try again.
   - If you see `'python' is not recognized`: same as above — install Python from
     python.org, making sure to check "Add Python to PATH."

### Step 2: Create the Project Folder

In the same Command Prompt window, type each line below and press **Enter** after each:

```
mkdir C:\ti84-bridge
cd C:\ti84-bridge
```

You are now inside the project folder. All future commands assume you are here.

### Step 3: Install the Required Python Packages

Type this and press **Enter**:

```
pip install pyserial anthropic python-dotenv
```

You will see text scrolling as packages download. Wait for it to finish. The last
line should say something like `Successfully installed ...`. If you see an error,
try:

```
python -m pip install pyserial anthropic python-dotenv
```

### Step 4: Create Your API Key File

The bridge needs an API key to talk to Claude. Here is how to get one:

1. Go to **console.anthropic.com** in your web browser.
2. Sign up for an account (or log in if you already have one).
3. In the left sidebar, click **"API Keys"**.
4. Click **"Create Key"**, give it a name like `ti84-bridge`, and click Create.
5. **Copy the key immediately** — it starts with `sk-ant-` and you will not be able
   to see it again after you leave the page.

Now create the key file:

1. Open **Notepad** (press Windows key, type `notepad`, press Enter).
2. Type exactly this, replacing the placeholder with your actual key:
   ```
   ANTHROPIC_API_KEY=sk-ant-your-actual-key-here
   ```
3. Click **File → Save As**.
4. Navigate to `C:\ti84-bridge`.
5. In the "File name" box, type `.env` (just dot, then env — no other text).
6. In the "Save as type" dropdown, select **All Files (*.*)**.
7. Click **Save**.

> **Note:** If your file ends up named `.env.txt`, Windows added a hidden `.txt`
> extension. Fix this: in the same folder in File Explorer, click View → check
> "File name extensions," then rename the file by removing `.txt` from the end.

### Step 5: Verify Your Files Are In Place

The bridge.py, echo_test.py, and other scripts should already be in `C:\ti84-bridge`
if you cloned or downloaded the project. In Command Prompt, type:

```
dir C:\ti84-bridge
```

You should see `bridge.py`, `echo_test.py`, and your `.env` file listed.

---

## Part 2: Install CE C Toolchain (about 10 minutes)

> **Current workflow (Oct 2026):** the toolchain is *not* installed on the Windows side
> (Defender blocks the installer). The program is built by GitHub Actions on every push
> (`.github/workflows/build.yml`, toolchain v14.2): `git push`, then
> `gh run list --workflow=build.yml --limit 1`, `gh run watch <id> --exit-status`, delete the old
> `calc\bin\AICHAT.8xp`, and `gh run download <id> --name AICHAT --dir calc/bin/`.
> The same toolchain is also installed in WSL Ubuntu (`~/CEdev`), so
> `wsl -d Ubuntu -e bash -lc 'export PATH=$HOME/CEdev/bin:$PATH; cd /mnt/c/ti84-bridge/calc && make'`
> builds the identical `.8xp` locally. Steps 6-12 below describe the original Windows route.

The CE C Toolchain is a free compiler that turns C code into a program your
calculator can run. Think of it like a translator.

### Step 6: Download the Toolchain

1. Open your web browser and go to:
   ```
   https://ce-programming.github.io/toolchain/
   ```
2. Click the **"Releases"** link (or look for a button labeled "Download" or
   "Latest Release").
3. You will land on the GitHub Releases page. Look for a file ending in `.exe`
   under "Assets." It will be named something like
   `CEdev-Windows-v11.x.x-installer.exe`.
4. Click it to download. The file is about 50–100 MB.

### Step 7: Run the Installer

1. Find the downloaded `.exe` file (probably in your Downloads folder).
2. Double-click it to run the installer.
3. If Windows shows a security warning saying "Windows protected your PC," click
   **"More info"** and then **"Run anyway."** This is normal for open-source software
   that is not signed with an expensive certificate.
4. Click through the installer, accepting all defaults. It installs to
   `C:\CEdev` by default. **Do not change this location.**
5. Click **Finish** when done.

### Step 8: Verify the Toolchain Works

> **Important:** Close any open Command Prompt windows and open a brand new one.
> This is necessary because the installer updates your PATH variable, and old Command
> Prompt windows will not see the change.

1. Press the **Windows key**, type `cmd`, press **Enter**.
2. Type:
   ```
   make --version
   ```
3. You should see something like:
   ```
   GNU Make 4.x.x
   ```
4. If you see that: wonderful, move to Part 3.
5. If you see `'make' is not recognized`: the installer did not add the tools to
   your PATH. Fix it manually with the steps below.

### Step 9: Manually Add the Toolchain to PATH (only if Step 8 failed)

1. Press the **Windows key** and type **"environment variables"**.
2. Click **"Edit the system environment variables"** in the search results.
3. In the System Properties window that opens, click the **"Environment Variables..."**
   button near the bottom.
4. In the **top half** of the Environment Variables window (labeled "User variables
   for [your name]"), click on the row labeled **Path**, then click **Edit**.
5. In the Edit window, click **New** on the right side.
6. Type:
   ```
   C:\CEdev\bin
   ```
7. Click **OK** to close each window until you are back at the desktop.
8. Open a **new** Command Prompt and try `make --version` again.

---

## Part 3: Build the Calculator Program (about 5 minutes)

### Step 10: Navigate to the Calc Folder

In Command Prompt:

```
cd C:\ti84-bridge\calc
```

### Step 11: Run Make

Type:

```
make
```

Press **Enter**. You will see output scrolling by. A successful build looks something
like this:

```
[CC] src/main.c
[LD] AICHAT.elf
[BIN] AICHAT.8xp
```

The exact messages may differ slightly. The important thing is that **no red error
messages appear** and the process finishes without the word "Error."

### Step 12: Confirm the Output File Exists

```
dir C:\ti84-bridge\calc\bin
```

You should see **AICHAT.8xp** listed. This is the compiled calculator program.

### If the Build Fails

- Double-check that `make --version` works in this Command Prompt window. If not,
  go back to Step 9.
- Look at the first error message (not the last). It usually tells you the exact
  problem. Common issues:
  - `srldrvce.h: No such file or directory` — the toolchain version you downloaded
    is older than v11. Check the toolchain website for a newer release, or
    follow the instructions in the design document to manually install CE-serial.
  - `No rule to make target` — you are in the wrong folder. Make sure you ran
    `cd C:\ti84-bridge\calc` first.

---

## Part 4: Send the Program to Your Calculator (about 5 minutes)

### Step 13: Install TI Connect CE

1. Open your web browser and go to:
   ```
   https://education.ti.com/en/products/computer-software/ti-connect-ce-sw
   ```
   Or simply search Google for **"TI Connect CE download"** — TI's official page
   should be the first result.
2. Download the Windows version and run the installer.
3. Accept all defaults. This is official TI software.

### Step 14: Connect the Calculator with the STANDARD TI Cable

Use **the cable that came with your calculator** (not the OTG adapter — that comes
later). Plug the Mini-USB end into the calculator and the USB-A end into your PC.

### Step 15: Open TI Connect CE and Send the File

1. Open TI Connect CE from the Start menu.
2. Click the **"Calculator Explorer"** icon (it looks like a folder or calculator).
   Your calculator should appear in the left panel after a few seconds.
3. Open File Explorer (press **Windows key + E**) and navigate to
   `C:\ti84-bridge\calc\bin`.
4. Drag **AICHAT.8xp** from that File Explorer window into the TI Connect CE window.
5. TI Connect CE will transfer the file. A progress bar appears briefly.
6. On your calculator, press **[PRGM]** and scroll to find **AICHAT** in the list.
   If it appears, the transfer worked.

### Step 16: CLOSE TI Connect CE Completely

This step is easy to forget and will cause problems later. Do it now:

- Right-click TI Connect CE in the taskbar and choose **Close Window**, or
- Click the X in the top-right corner of the TI Connect CE window.

> **Why:** TI Connect CE and the chat bridge both try to control the calculator's
> USB connection. They will fight each other if both are open. You must close TI
> Connect CE every time before running the bridge.

---

## Part 5: Run AICHAT on the Calculator

### Step 17: Keep the Standard Cable Plugged In

Your TI cable is already connected from Part 4. Leave it plugged in — the same cable
works for both transferring programs and running the chat bridge.

### Step 18: Run the AICHAT Program on the Calculator

1. On the calculator, press **[PRGM]**.
2. Scroll down to **AICHAT** and press **[ENTER]**.
3. Press **[ENTER]** again to run it.
4. The screen shows the **ZACH GPT** splash: blue background with falling characters,
   the orange title, and at the bottom `WAITING FOR USB CONNECTION...`.

### Step 19: Find the COM Port Number

1. Press the **Windows key** and type **"Device Manager"**. Open it.
2. Look for a section called **"Ports (COM & LPT)"** — click the arrow to expand it.
3. You should see an entry like **"USB Serial Device (COM3)"**. The number after COM
   is your port number. Write it down — you will need it in the next step.

> **If nothing appears under Ports (COM & LPT):** Windows is installing a driver
> in the background. Wait 30 seconds and check again. If it still does not appear,
> see the Troubleshooting section at the end of this guide.

---

## Part 6: Echo Test — Verify the Connection (about 2 minutes)

Before involving the AI, let's make sure the USB connection works. An echo test
simply sends back whatever the calculator sends, so you can confirm data is
flowing.

### Step 20: Run the Echo Test

1. Open Command Prompt.
2. Navigate to the project folder:
   ```
   cd C:\ti84-bridge
   ```
3. Run the echo test, replacing `COM3` with your actual COM port number:
   ```
   python echo_test.py COM3
   ```
   You should see:
   ```
   Echo test running. Send text from calc.
   ```

### Step 21: Test from the Calculator

1. On the calculator, press **[ENTER]** on the splash once it says `USB CONNECTED`, then
   type a word with the T9 keys (tap **[8]** once for A, twice for B, three times for C;
   see the Keyboard Cheat Sheet below).
2. Press **[ENTER]**.
3. The word should appear in the Command Prompt window on your PC, prefixed with
   `Received:`.
4. It should also appear back on the calculator screen.

### Step 22: Stop the Echo Test

Press **Ctrl+C** in the Command Prompt window to stop the echo test.

### If the Echo Test Does NOT Work

- Make sure TI Connect CE is fully closed (Step 16).
- Make sure the calculator is running the AICHAT program (the screen says
  "Waiting for USB..." or shows a prompt).
- Double-check the COM port number in Device Manager — it may have changed.
- Try a different USB port on your PC.
- See the full Troubleshooting section at the end of this guide.

Only proceed to Part 7 once the echo test works.

---

## Part 7: Run the AI Bridge (The Fun Part!)

### Step 23: Start the Bridge

In Command Prompt:

```
cd C:\ti84-bridge
python bridge.py COM3
```

(Replace `COM3` with your port number.)

You should see:

```
Bridge running on COM3. Waiting for calc...
```

### Step 24: Ask a Question!

1. On the calculator, the splash should now read `USB CONNECTED` — press **[ENTER]** to
   open the typing screen (orange header, `>` prompt, 12-key grid).
2. Type your question with **T9 multi-tap** on the number keys: tap a key to cycle through
   its letters (the big orange box shows the pending letter), pause 0.9 s or press another
   key to commit. **[0]** is space. **[ALPHA]** switches to `123` mode, where the first tap
   of a key is the digit and the second tap is its symbol (`12%` = ALPHA, 1, 2, 1, 1).
3. Press **[ENTER]** to send.
4. The calculator shows your question in a bubble with a `THINKING...` pill while the PC
   contacts Claude AI. **[CLEAR]** cancels and puts the question back in the input.
5. Wait **3 to 8 seconds**.
6. The answer appears in an orange bubble. **[UP]/[DOWN]** scroll long answers.
7. Press **[ENTER]** to ask another question (the conversation stays on screen), or
   **[CLEAR]** / **[2nd]** to return to the splash.

---

## Keyboard Cheat Sheet

The letters follow the **physical keypad** (7 8 9 on the top row), so the alphabet reads
top-down, left-to-right:

| Key | ABC mode (tap to cycle) | 123 mode (1st tap / 2nd tap) |
|-----|-------------------------|------------------------------|
| **[7]** | `.` `,` `?` `!` `'` | `7` / `+` |
| **[8]** | `A` `B` `C` | `8` / `-` |
| **[9]** | `D` `E` `F` | `9` / `*` |
| **[4]** | `G` `H` `I` | `4` / `(` |
| **[5]** | `J` `K` `L` | `5` / `)` |
| **[6]** | `M` `N` `O` | `6` / `,` |
| **[1]** | `P` `Q` `R` `S` | `1` / `%` |
| **[2]** | `T` `U` `V` | `2` / `$` |
| **[3]** | `W` `X` `Y` `Z` | `3` / `.` |
| **[0]** | space | `0` / `/` |

| Key | What It Does |
|-----|--------------|
| **[ALPHA]** | Switch between `ABC` and `123` mode (commits the pending letter first) |
| **[DEL]** | Drops the pending letter if there is one, otherwise deletes the last character |
| **[ENTER]** | Splash: start typing. Typing: send. Answer: ask a new question |
| **[UP]/[DOWN]** | Scroll the answer by 20 px |
| **[CLEAR]** | Thinking: cancel. Answer: back to splash. Splash: exit the program |
| **[2nd]** | Typing or Answer: back to the splash (clears the conversation) |
| **[ON]** | Emergency exit — press this if the program freezes or hangs |

**Tip:** pause 0.9 s between two letters that share a key (`AB` = tap 8, wait, tap 8 twice),
or press any other key to commit immediately.

---

## Troubleshooting

### "USB init failed" appears on the calculator screen

**Cause:** The USB cable is not seated properly, or there is a driver conflict.

**Fix:**
1. Press **[ON]** to exit the program.
2. Unplug the USB cable from the calculator.
3. Wait 5 seconds.
4. Plug it back in firmly.
5. Run AICHAT again.

---

### "Serial error" or "COM port not found" in the Command Prompt

**Cause:** Either TI Connect CE is still open, the COM port number changed, or the
USB connection is not creating a serial device.

**Fix:**
1. Close TI Connect CE completely.
2. Open Device Manager and check Ports (COM & LPT) for the current COM port number.
3. Make sure the calculator is running AICHAT (not just on the home screen).
4. Update the COM port in your command: `python bridge.py COM4` (or whatever number
   Device Manager shows).

---

### No response after 90 seconds

**Cause:** Usually a network or API key issue.

**Fix:**
1. Check that your PC has an active internet connection (can you browse the web?).
2. Open `C:\ti84-bridge\.env` in Notepad and verify the API key is there and looks
   correct (starts with `sk-ant-`).
3. Go to console.anthropic.com and verify the key is still active and has not been
   deleted.
4. Check that you have credit/billing set up on your Anthropic account — new accounts
   may need to add a payment method.

---

### "USB Serial Device" does not appear in Device Manager

**Cause:** Windows cannot install the driver, or the USB connection is not working.

**Fix:**
1. Make sure the calculator is actually running AICHAT (the program must be running
   for the serial device to appear — the calculator does not expose the port until the
   program initializes it).
2. Try a different USB port on your PC.
3. Try a different USB cable if available.
4. Unplug, wait 10 seconds, replug and run AICHAT again.

---

### Garbled or garbage characters on the calculator screen

**Cause:** Non-ASCII characters in the response (em-dashes, curly quotes, etc.) that
the calculator cannot display.

**Fix:** The bridge.py script automatically replaces the most common offenders. If
you still see garbage, press any key to dismiss the response and ask again. This is
rare.

---

### TI Connect CE says "No calculator found" when transferring the program

**Cause:** Wrong cable, or calc needs to be on the home screen.

**Fix:**
1. Make sure you are using the **standard TI cable** (not the OTG adapter) for this step.
2. Press **[2nd] + [MODE]** on the calculator to get to the home screen.
3. Unplug and replug the cable.
4. Try a different USB port on your PC.

---

### The build fails with "srldrvce.h: No such file or directory"

**Cause:** Your CE Toolchain installation is older than version 11 and does not
include the serial driver header.

**Fix:**
1. Check the toolchain version at `https://ce-programming.github.io/toolchain/`.
2. If a newer version is available, uninstall the old one and install the new one.
3. If you must use an older version, visit `github.com/commandblockguy/CE-serial`
   and follow the manual installation instructions to add `srldrvce.h` to
   `C:\CEdev\include\`.

---

## Quick Reference

You use the **same standard TI USB cable** for everything — transferring the program
and running the chat bridge. Just remember to **close TI Connect CE** before running
the bridge.

---

## ZACH GPT v5.0 Screens

| Screen | What you see | Keys |
|--------|--------------|------|
| **Splash** | Blue background, falling characters, orange `ZACH GPT` title, status line `WAITING FOR USB CONNECTION...` → `USB CONNECTED` | ENTER (or any digit / ALPHA) once connected → Typing. CLEAR exits. |
| **Typing** | Orange header with `ABC`/`123` badge, `>` prompt with the last two lines of your question, 12-key T9 grid laid out like the keypad (`7 8 9 / 4 5 6 / 1 2 3 / ALPHA 0 DEL`) | Digits type, ALPHA toggles mode, DEL backspaces, ENTER sends, 2nd exits to the splash. |
| **Thinking** | Your question in a light-blue bubble, orange `THINKING...` pill with a spinner | CLEAR cancels and restores the question. |
| **Answer** | Conversation bubbles (last 6 messages), orange scrollbar on the right | UP/DOWN scroll, ENTER new question, CLEAR or 2nd back to the splash. |

If the USB cable is unplugged on any screen, the program returns to the splash with
`USB DISCONNECTED`; plug it back in and it flips to `USB CONNECTED` again.

---

## You're Done!

If everything is working, you should now be able to:

1. Run AICHAT on your calculator.
2. Run `python bridge.py COM3` on your PC.
3. Type any question on the calculator, press ENTER, and get an AI response in seconds.

**Enjoy!** If you run into issues not covered above, check the design document
at `C:\dev\home\docs\plans\2026-03-18-ti84-ai-chat-design.md` for deeper technical
details.
