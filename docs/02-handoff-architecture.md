# Handoff: architecture and spec session

For a fresh Claude session (Opus) in `~/Documents/Enji_Nearing/console-led`. Written 2026-11-07.

## Read first
1. `docs/01-intent.md`: the confirmed statement of intent. It governs; do not re-interview.
2. `docs/00-lamp-os-notes.md`: lamp-os facts and what carries over from the old repo.
3. Only if a fact is missing: the lamp-os clone at `~/Documents/Enji_Nearing/_reference/lamp-os`
   (start with `docs/dev/networking.md`). Do not read the whole repo; it is large.

## Decisions confirmed after the intent (2026-11-07)
These sharpen the intent. Record them as ADRs (see step 4).
1. **Courier follows lamp-os relay rules exactly.** Our nodes relay only what a lamp would relay,
   with the same dedup. `MSG_EVENT` / `MSG_COMMAND` stay nearby-only. No relay storms.
2. **Courier and interactivity are separate settings.** Relaying lamp traffic is network
   plumbing and is always on. Reacting to lamps is behaviour, set per installation (closed by
   default). A closed installation still relays lamp packets but never acts on them.
3. **Lamps won't carry our packets until upstream accepts a new relayed type.** Lamps drop
   unknown types. Until then our traffic goes node to node only. Fine for milestone 1, since
   nodes are spaced to reach each other.
4. **"Password protected" = signed messages, not radio encryption.** Lamp HELLOs are plaintext
   broadcasts we must keep hearing. Our control messages carry a tag from a per-installation key
   (like lamp-os `command_auth`). "Only Nick can edit" is enforced by who holds the key; the
   public tier gets its own limited key.
5. **Venue Wi-Fi is a last resort, gateway only. Discuss later; Nick deferred the detail.**
   - The system never uses venue Wi-Fi as its primary communications network.
   - A venue router is only joined, as a last resort, so one gateway node can reach the internet.
   - Open: WLED's ESP-NOW runs on its Wi-Fi channel, so light-node APs must be pinned to channel
     6 and a venue router only works if it is on channel 6. Nick suspects this is where we
     diverge from lamp-os. Lay out the options (e.g. Pi/laptop gateway over USB serial or
     Ethernet, wired Ethernet node, LTE, channel-6-only router join) but **do not decide; ask
     Nick when he is fresh.**
   - 2026-11-13 input: `docs/03-gateway-hardware.md` proposes our own OpenWrt router (SamKnows
     Whitebox 8, AP pinned to channel 6, phone/LTE uplink over USB). Feed it into idea-refine.

## Run these four skills, in this order
1. **`/agent-skills:idea-refine`** on the open architecture questions from the intent:
   - Console transport and network architecture for under 30 ms on site (standalone app, not a
     browser app).
   - Where the input-to-effect fan-out list lives: sender or receivers.
   - Gateway design (see decision 5; options only).
   - Remote OTA over the mesh (later milestone, but don't paint it into a corner).
   - Shape of the new relayed message type to propose upstream to lamp-os.
   Diverge, then converge to a shortlist with trade-offs. Ask Nick to pick where it matters.
2. **`/agent-skills:source-driven-development`** to check the facts the shortlist rests on
   against WLED and Espressif sources before the spec commits to them:
   - WLED 16 ESP-NOW: how usermods receive frames, whether foreign magics/broadcasts reach
     `onEspNowMessage()`, v1 vs v2 frame size, sending from a usermod.
   - Channel rules: AP channel pinning, what happens when STA joins a router on another channel,
     WLED's reconnect behaviour (it restarts its own AP when searching).
   - ESP-NOW latency and coexistence with WLED's Wi-Fi AP serving the Console.
3. **`/agent-skills:spec`** (spec-driven-development): write `docs/SPEC.md`, decomposed into a
   capability map (mesh/courier, identity, messaging + auth, input/mapping, WLED bridge, Console,
   gateway, OTA). Milestone 1 (two controllers, physical input changes the other's WLED effect)
   must be fully specified; later capabilities can stay at interface level.
4. **`/agent-skills:documentation-and-adrs`**: one ADR per locked decision in `docs/adrs/`,
   starting with decisions 1-4 above, then each decision the spec locks. Mark decision 5
   "Proposed / open".

Then hand off to `/agent-skills:plan` for task breakdown.

## Working rules
- Nick prefers simplicity: no ceremony that doesn't pay off for small ESP32/Pi work.
- Don't copy lamp-os source (no licence yet). Interoperate from their docs.
- Ask before locking choices that materially affect the project; present trade-offs.
- Keep each session's context small: link to files rather than pasting them.
