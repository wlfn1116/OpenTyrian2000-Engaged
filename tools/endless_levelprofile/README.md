# Endless level-profile generator

`gen_profile.py` generates `src/endless_levelprofile.h`. The table adds a small
level-specific adjustment to Endless danger ranks and payouts.

The source data comes from
[Tyrian2000Atlas](https://github.com/wlfn1116/Tyrian2000Atlas), which simulates
each level at every difficulty without player fire.

## Regenerate

Run this only when the data or mapping changes:

```sh
DOTNET_ROLL_FORWARD=Major dotnet \
  /path/to/Tyrian2000Atlas.dll \
  --exportthreat threat.csv /path/to/OpenTyrian2000-widescreen/data

python gen_profile.py
```

`threat.csv` is committed, so regenerating the header does not require the Atlas
checkout.

## Mapping

```text
baseDanger = clamp(round((Difficulty01 - 1.0) * 4.0), -2, 5)
```

`Difficulty01 == 1.0` is neutral. The adjustment remains small so sector
modifiers dominate the final rank.

`lengthClass` is 0 for short, 1 for normal, and 2 for long levels.

The CSV retains the component measurements for review and retuning. The generator
primarily uses `difficulty01` and measured duration.
