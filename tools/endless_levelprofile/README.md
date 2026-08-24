# Endless level profile

`gen_profile.py` turns measured level difficulty into
`src/endless_levelprofile.h`. Endless uses the result as a small adjustment to
danger and payout.

The measurements come from
[Tyrian2000Atlas](https://github.com/wlfn1116/Tyrian2000Atlas), which simulates
every level and difficulty without player fire.

## Regenerate

Run the Atlas only when its data or the level mapping changes:

```sh
DOTNET_ROLL_FORWARD=Major dotnet \
  /path/to/Tyrian2000Atlas.dll \
  --exportthreat threat.csv /path/to/OpenTyrian2000-Engaged/data

python gen_profile.py
```

`threat.csv` is committed, so regenerating the header does not otherwise need
an Atlas checkout.

## Mapping

```text
baseDanger = clamp(round((Difficulty01 - 1.0) * 4.0), -2, 5)
```

`Difficulty01 == 1.0` is neutral. The range stays small so sector modifiers
remain the main source of danger.

`lengthClass` is 0 for short, 1 for normal, and 2 for long levels. The CSV keeps
the component measurements for review; the generator mainly reads
`difficulty01` and measured duration.
