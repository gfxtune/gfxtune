# Security policy

## Reporting a vulnerability

Please report security problems privately, through GitHub's
"Report a vulnerability" button on the repository's Security tab, not in a
public issue. Say what is affected, how it can be triggered, and what an
attacker gains. You will get an answer within a week; gfxtune is maintained
by volunteers.

Fixes are made for the latest release only.

## What is in scope

gfxtune runs in a few places where a mistake matters more than in the
window itself:

- `gfxtune-run`, which every game with gfxtune in its Steam launch options
  starts through, and which sets environment variables and starts other
  programs before the game;
- the Vulkan layer, which runs inside games and reads gfxtune's settings
  files while they run;
- the pacman hook, which runs as root when gfxtune is removed and runs
  `gfxtune --remove-launch-lines` as each user with a Steam folder;
- the editing of Steam's `localconfig.vdf`, where only the launch options
  gfxtune manages may change.

gfxtune never needs root itself, never opens network connections, and only
reads the amdgpu driver's files in `/sys`; it does not write to them.
