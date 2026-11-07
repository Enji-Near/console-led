# console-led: Statement of Intent

Confirmed 2026-11-07 via interview.

## Outcome
A new project, **console-led**. WLED stays the light engine, with custom usermods. Rebuilt from the
ground up: networking, device identity, and a standalone Console.

- **Networking**
  - Nodes sit on the lamp-os mesh (ESP-NOW, channel 6) and always act as couriers, passing packets
    to and from lamps.
  - Nodes are spaced so node-to-node links always work. Lamps move, so they are redundancy only.
  - Carrying our packets across lamps needs a new relayed message type, to be developed and
    proposed upstream to lamp-os.
  - At least one gateway node provides internet access for remote use.
- **Lamp interaction**
  - Closed by default. An interactivity level is set per installation, from closed through
    partly open to fully lamp-driven.
- **Console**
  - Standalone app (not a browser app), targeting sub-30 ms control latency on site. Transport and
    network architecture are deferred to a deep architecture session.
  - Source of truth, holding versioned (git-style) configs. Nodes keep their own copies so an
    installation runs with the Console off.
  - A node that falls behind the latest version can be updated or left as is, per node. Cloned
    nodes (e.g. several Glorious-Holes) may drift.
  - Device discovery and dashboard; per-preset editing with faders; saving defaults.
  - Input-to-effect mapping where one input can fan out to several targets with different values
    (Dialer 7 -> Glorious-Hole preset 7 and Halloween-Matrix preset 8). Whether the fan-out list
    lives on the sender or the receivers is for the spec to decide.

## User
Nick, as installer and operator. Friends in the lamp-os community are the eventual wider audience.

## Why now
No deadline. A solid mesh is the core of the project's success. Nick does not want to engineer a
mesh himself when friends already have one they want others to use. He is close to the lamp-os
community and expects the authors to be open to integration.

## Success
- First milestone: two controllers on the mesh; one takes physical input and changes the WLED
  effect on the other.
- Then: the Console finds and configures nodes, and the Dialer 0..9 mapping works.
- Later: full remote operation, including remote OTA of firmware (a requirement, but it can land
  after the first milestones).

## Constraints
- Initial scale: 1-10 nodes per installation. Festival scale (100+ lamps over a huge area) comes later.
- Interoperate with lamp-os using its docs as the spec until the authors add a license or give
  permission. Do not copy their source.
- Control latency under 30 ms on site.

## Security
- Only Nick can edit or flash. Everyone else is locked out unless access is granted.
- Every generated ESP32 network is password protected.
- Public access is an opt-in, limited, sandboxed tier (a few presets or settings).
- Lamp access follows the same model.

## Repo
New repo `console-led`. Old history is ported in as needed. No references to the old repo unless
they add necessary context.

## Out of scope
- A new light renderer.
- Rebuilding or replacing lamp-os, or changing lamp firmware.
- Voice between Dialers (stretch goal only).
- Festival-scale design for now.
- Remote OTA in the first milestones.

## Open for the architecture session
- Console transport and network architecture for the sub-30 ms target.
- Where the mapping fan-out list lives (sender vs receivers).
- Gateway design: internet access vs the channel-6 constraint.
- Remote OTA over the mesh.
- A new relayed message type for lamp-os, and the authors' license and command-key position.
