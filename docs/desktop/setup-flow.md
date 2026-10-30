# NeolyxOS Setup Assistant: first-run flow

Status: implemented in userspace (`desktop/shell/views/setup_*.c`).
Owner area: Setup flow.
Design source: designed in code. The owner's Figma file (NeolyxOS, node 0-1)
could not be read in this session because the Figma connector hit its plan rate
limit. Compare against Figma when access returns.

## Where it lives

| File | Job |
|------|-----|
| `desktop/include/setup_assistant.h` | Public hooks called by `desktop_shell.c` (unchanged) |
| `desktop/shell/views/setup_assistant.c` | Flow state, page transitions, step indicator, footer, all input |
| `desktop/shell/views/setup_pages.c` | Layout, drawing and logic for each page |
| `desktop/shell/views/setup_store.c` | Validation, data tables, persistence, the network and accounts client seams, secure wipe |
| `desktop/shell/views/setup_internal.h` | Private interfaces shared by the three files |

The shell calls `setup_assistant_init()` once at boot. While
`setup_assistant_active()` is true, the shell ticks and renders the assistant
above everything except the cursor and sends it all mouse and keyboard input.

## Activation

```
boot ─► setup_assistant_init()
          │
          ├─ NX_HOST_PREVIEW ───────────────► inactive (scripts: "call setup_assistant_show")
          │
          ├─ /System/Config/setup_complete.nlc exists ─► inactive
          │
          └─ marker missing ─────────────────► active, page = Welcome

Settings "Run setup again" ─► setup_assistant_show() ─► fresh model, page = Welcome
```

## Flow diagram

```
 ┌─────────┐   ┌──────────┐   ┌──────────────┐   ┌───────────────┐   ┌─────────┐
 │ Welcome │──►│ Language │──►│ Region &     │──►│ Accessibility │──►│ Network │
 │ (Hello) │   │  step 1  │   │ keyboard  2  │   │      3        │   │    4    │
 └─────────┘   └──────────┘   └──────────────┘   └───────────────┘   └────┬────┘
                                                                          │ Join / Set Up Later
 ┌──────┐   ┌────────────┐   ┌──────────────┐   ┌──────────┐   ┌──────────▼───┐
 │ Done │◄──│ Setting up │◄──│ Appearance 8 │◄──│ Privacy 7│◄──│ Account   5  │
 └──┬───┘   │ (no input) │   └──────────────┘   └──────────┘   └──────┬───────┘
    │       └────────────┘          ▲                                  │ valid only
    │                               └──── Ketivee ID 6 (optional) ◄────┘
    ▼
 assistant fades out, desktop is live
```

Back (button or Esc) returns one page, from Language through Appearance.
Welcome, Setting up and Done have no Back. Continue (button or Enter) runs the
page's validation first; if it fails, the page shows inline errors and stays.

## Chrome (every page)

- Backdrop: the pre-blurred wallpaper over the whole screen with a dim layer
  (dark: 43% near-black; light: 27% near-white). Menu bar, dock and windows
  are hidden behind it.
- Card: 760 x 580 (shrinks to the screen minus 32 px each side), radius 22,
  `nxglass_panel(..., NXGLASS_THICK, elevation 4)`. It rises 18 px and fades
  in on show and fades out on finish.
- Step indicator: 8 dots for steps 1 to 8. The current step is a 22 px accent
  pill, finished steps are dimmed accent and later steps are grey. A spring
  moves the pill between steps.
- Page transition: the outgoing page slides 70 px away and fades out while
  the incoming page slides in from the other side (`nxui_spring_step`,
  stiffness 220, critically damped). Input waits until the page settles.
  With Reduce motion on, the page changes at once with no slide or spring.
- Footer: hairline, Back (left), "Step N of 8" (center), an optional
  secondary button, then the primary button (right). Buttons act on release.

## Keyboard

| Key | Action |
|-----|--------|
| Tab / Shift+Tab | Next / previous field, list or switch on the page |
| Up / Down | Change the focused vertical list |
| Left / Right | Change the focused horizontal choice (appearance, accent, Sign In/Create) |
| Space | Flip the focused switch |
| Enter | Continue (same as the primary button) |
| Esc | Back |
| Backspace | Delete the last character in the focused field |

When a page opens, its first text field gets focus, or its first control if it
has no fields. Focus rings on lists and switches appear only after you use the
keyboard. A mouse click hides them again.

## Pages

### 0. Welcome
- A greeting in large display type, cycling every 2.6 s through Hello,
  Namaste, Hola, Bonjour, Hallo, Ciao, Olá, Konnichiwa, Ni hao, Salaam, Hej,
  Merhaba. Each word fades and rises in, then fades out. With Reduce motion
  the words still change, without the movement.
- Below it: "Welcome to NeolyxOS", plus a soft accent glow behind the greeting.
- Footnote: "Setup takes a few minutes. Use Tab, Enter and Esc to move through it with the keyboard."
- Primary: Get Started.

### 1. Language
- List: English (US), English (UK), English (India), Hindi, Español, Français,
  Deutsch, Português (Brasil). Default: English (US).
- Persisted: `locale.language` (e.g. `en_US`).
- Note: the desktop has no string catalog yet. This choice is saved for the
  localization service but does not change UI text today.

### 2. Region and keyboard
- Two lists side by side: Region (United States, India, United Kingdom,
  Germany, France, Spain, Brazil) and Keyboard layout (US, US International,
  British, German QWERTZ, French AZERTY, Spanish, Brazilian ABNT2).
- Choosing a region also selects its usual keyboard layout, until you pick a
  layout yourself.
- Persisted: `locale.region`, `locale.keyboard`.

### 3. Accessibility
- Switches (all off): Larger text, Bold text, Reduce motion, Increase
  contrast, Reduce transparency. Each has a one-line explanation.
- Reduce motion takes effect in the assistant at once. The other switches are
  saved for the shell to apply.
- Persisted: `accessibility.*`.

### 4. Network
- Calls `setup_network_scan()` (the lead wires it to `nxnet.h`).
  - Real system today: returns `SETUP_ERR_UNSUPPORTED`. The page shows
    "Networking isn't available yet / You can connect later in Settings >
    Network", and the primary button reads Set Up Later.
  - Preview: a demo list (Ethernet, 2 secured and 1 open Wi-Fi), compiled only
    under `NX_HOST_PREVIEW`.
- Selecting a secured Wi-Fi network shows a masked "Network password" field.
  Primary: Join (calls `setup_network_join()`), Secondary: Set Up Later.
- Join errors show inline: missing password, join failed, or not available yet.
  The network password is wiped after every join attempt and whenever you
  pick a different network.
- Persisted: `network.preferred` (the SSID, not logged), only after a successful join.

### 5. Create your account
| Field | Rule (message) |
|-------|----------------|
| Full name | Required; printable ASCII; at least one letter |
| Account name | Filled in from the full name (lowercase a-z0-9) until you edit it. Required; 1 to 31 characters; starts with a-z; only a-z 0-9 . _ - ; reserved names (root, admin, administrator, system, guest, daemon, nobody, kernel, neolyx, setup, user, users) are rejected |
| Password | Masked with bullets. At least 8 characters; not on the common-password list; strength at least Fair |
| Verify | Must match, compared in constant time |
| Hint | Optional; must not contain the password |

- Strength meter (4 segments plus a label) under the password while you type:
  Too short, Weak, Fair, Good, Strong. A password that contains the account
  name or a part of the full name counts as Weak.
- An error shows once you leave a field you have typed in, and on every field
  after you press Continue. Each error appears as red text with a warning icon
  under the field, and the field gets a red outline. When there is no error,
  a field may show helper text instead.

### 6. Ketivee ID (optional)
- Segmented control: Sign In / Create Ketivee ID. Fields: E-mail, Password (masked).
- No network calls. Validation: e-mail format, a password is required, and
  Create needs at least 8 characters.
- If both fields are empty, Continue skips this step. Set Up Later clears both
  fields and skips.
- With valid input, the password is wiped at once and the e-mail is kept as
  `ketivee.pending_email` (not logged). The Done page says "Finish when
  online". The footnote on this page states that nothing is sent and the
  password is not stored.

### 7. Privacy and analytics
- Switches, all off by default: Share analytics with Ketivee, Share crash
  reports, Personalized suggestions, Location services.
- Footnote: "You can review these at any time in Settings > Privacy."
- Persisted: `privacy.*`.

### 8. Appearance
- Three preview tiles (Light, Dark, Auto: half light, half dark), each a small
  desktop picture that uses the chosen accent color.
- Choosing a tile applies it at once through `nxui_set_appearance()`. Auto
  means light from 07:00 to 19:00 by the real-time clock, dark otherwise.
- Accent swatches: Saffron, Blue, Teal, Green, Purple, Pink, Red, Graphite.
  NXUI has no way to set the accent live yet, so the choice shows in the tile
  previews and is saved (`desktop.accent`, `desktop.accent_color`).
- Primary: Set Up.

### 9. Setting up (no input)
Four tasks run one after another, about 0.6 s apart. A progress bar and a list
of task rows show where it is: grey ring (waiting), spinner (running), green
check (done), amber "!" with "Not available yet" (unsupported), or red "!"
with "Failed" (error).
1. Creating your account calls `setup_accounts_create()`, then **wipes the
   password and verify buffers whatever the result**.
2. Saving your preferences calls `setup_store_save_prefs()`, which writes
   through nxconfig.
3. Applying your appearance.
4. Finishing up calls `setup_store_write_marker()`.

The assistant moves on to Done by itself 0.8 s after the last task.

### 10. Done
- Green check tile, "You're all set", "Welcome to NeolyxOS, <first name>."
- Summary card: Account, Ketivee ID, Network, Appearance.
- An amber notice appears when something did not finish: no account service
  (so no password was set), a marker write failure (setup will open again), or
  a failed preferences save.
- Primary: Start Using NeolyxOS. The assistant fades out, wipes every secret
  field again and hands input back to the desktop.

## Security and persistence

- No accounts syscall or userspace accounts service exists. The kernel's
  `user_auth` is reachable only inside the kernel. See
  `docs/desktop/proposals/setup.md`.
- Passwords (account, verify, Ketivee, Wi-Fi) live only in fixed 64-byte
  `setup_field_t` buffers. They are wiped with volatile stores:
  - after the account task,
  - after a Ketivee entry is accepted or skipped,
  - after every network join attempt,
  - when the assistant opens or closes.
- The bullet text drawn for a masked field goes into a stack buffer that is
  wiped right after drawing.
- Passwords, hints and e-mail addresses are never logged. Names and SSIDs are
  logged as "(set, value not logged)". Preference values are logged one line
  per key: `[SETUP] config <section>.<key> = <value>`.
- Saved through nxconfig (`NXC_SYSTEM_CONFIG`): `locale.*`,
  `accessibility.*`, `privacy.*`, `desktop.theme/accent/accent_color`,
  `account.full_name/name/pending_creation`, `ketivee.pending_email`,
  `network.preferred`, `setup.completed`.
- Marker: `/System/Config/setup_complete.nlc` is written with
  `nx_open(O_WRONLY|O_CREAT|O_TRUNC)` and also carries a copy of the
  non-secret choices, because `nxc_config_save()` has no VFS backend yet (it
  is a TODO in `lib/nxconfig.c`). Delete the file to run setup again at the
  next boot.
- Pending account: when account creation is unsupported, setup still
  completes and records `account.pending_creation = true`, so a later accounts
  service can ask the user to finish. This is a usability-versus-security
  choice that the owner should confirm (see the proposal).

## Preview

```
make -C desktop/tools/preview OBJ=out/setup
desktop/tools/preview/out/setup/preview -n 120 -o shot.png -s script.txt
# script.txt
5 call setup_assistant_show
6 call setup_assistant_preview_light   # optional, preview-only helper
60 shot welcome.png
61 key 1c                              # Enter = Get Started
```
