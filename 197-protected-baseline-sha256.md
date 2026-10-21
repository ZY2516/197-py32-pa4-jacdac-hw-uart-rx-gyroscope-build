# 197 Protected Baseline SHA-256 Manifest

Baseline:

`D:\py32-jacdac-rx-staging\PY32-DEMO-1\00-reference-docs\claude\194-py32-pa4-jacdac-hw-uart-rx-hybrid-build-3\194-py32-pa4-jacdac-hw-uart-rx-hybrid-build`

The following firmware inputs are byte-identical between the selected baseline
and build 197.

| Path | SHA-256 |
| --- | --- |
| `Src\py32_jacdac_runtime.c` | `EFA89369E8E75E147126B91FC771FB59C27CBB912279CCACDA37B3CAD2215B77` |
| `Inc\py32_jacdac_board.h` | `93250F10077B9F4D137F86967981136B0E03FDF0427C352CE6A40FF2619CCB3D` |
| `Src\main.c` | `6776A7C13C86B14558EA11E042C933678EBF17678F55F573A98D3AB15FEEA590` |
| `Src\py32f002b_it.c` | `7116C2991A9AF39F4313F616705FFF9C1FAE4CB944CF3338DC3CEE0A1644A5E3` |
| `Src\qmi8656_probe.c` | `6CAEE832D06CB13420016B093A99BD2CF8B8DF5806EFCBF5091C5A9B5C3C40AE` |
| `Inc\qmi8656_probe.h` | `177700EA397FA995CB399F314955264368C247F6B30BE8E13CE9E5FE6036DC26` |
| `Inc\jd_user_config.h` | `6CE015E189EF8826C348864C20C407F35F6CC970581355677201E5F968FB2C0A` |
| `MDK-ARM\Project.uvprojx` | `D1805F2790374B9AD58B6831B33C98065EBEA996C84AAD7943C772748D852D66` |

`third_party\jacdac-c` tree digest:

`9EA634D09AB605FED3B9648AFF3D2DBE8DDC04E527F786C01AD66809C71EC13C`

The tree digest is SHA-256 over sorted UTF-8 lines in the form
`<file-sha256><two spaces><relative-path-with-forward-slashes>`.

The only firmware source difference is:

| Path | Baseline SHA-256 | Build 197 SHA-256 |
| --- | --- | --- |
| `Src\py32_qmi8656_jacdac_services.c` | `2104CBD12A659E5A6183305CBC6631D0788FD63A45A5C2C077F4B03CAC982E14` | `D2CB4161B1F375A42782BDE7882659F2EDBDFFFFCF4DB2C02503F8A2834701C1` |

The source diff consists only of the two firmware-version literals changing
from `0.2.30-py32-gyro-rx-hybrid` to `0.2.1-py32-gyro-beacon`.
