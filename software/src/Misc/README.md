# Miscellaneous

## `pyocd.yaml`

This file contains the configuration for pyocd, which is used for flashing and debugging the firmware on the microcontroller. It specifies the target device, the interface to use, and other settings required for pyocd to function correctly with the hardware.

Uuse the following command to install the PY32 pack to your host :

```zsh
pyocd pack install PY32F003x8
```

You can also use the following command to find all the available PY32 packs :

```zsh
$ pyocd pack find PY32
0000276 I No pack index present, downloading now... [pack_cmd]
  Part           Vendor   Pack                Version   Installed  
-------------------------------------------------------------------  
  PY32F003x4     Puya     Puya.PY32F0xx_DFP   1.2.8     False      
  PY32F003x6     Puya     Puya.PY32F0xx_DFP   1.2.8     False      
  PY32F003x7     Puya     Puya.PY32F0xx_DFP   1.2.8     False      
  PY32F003x8     Puya     Puya.PY32F0xx_DFP   1.2.8     False      
  PY32F021x8     Puya     Puya.PY32F0xx_DFP   1.2.8     False      
  PY32F071xB     Puya     Puya.PY32F0xx_DFP   1.2.8     False      
  PY32F072Cx6    Puya     Puya.PY32F0xx_DFP   1.2.8     False      
  PY32F072Cx8    Puya     Puya.PY32F0xx_DFP   1.2.8     False      
  PY32F072Cx9    Puya     Puya.PY32F0xx_DFP   1.2.8     False      
  [...]     
  PY32MD410x8    Puya     Puya.PY32Mxxx_DFP   1.0.6     False      
  PY32MD420x8    Puya     Puya.PY32Mxxx_DFP   1.0.6     False      
  PY32MD430x8    Puya     Puya.PY32Mxxx_DFP   1.0.6     False      
  PY32T020Bx5    Puya     Puya.PY32T0xx_DFP   1.0.7     False      
  PY32T020Bx6    Puya     Puya.PY32T0xx_DFP   1.0.7     False      
  PY32T020x5     Puya     Puya.PY32T0xx_DFP   1.0.7     False      
  PY32T020x6     Puya     Puya.PY32T0xx_DFP   1.0.7     False      
  PY32T090xB     Puya     Puya.PY32T0xx_DFP   1.0.7     False      
  PY32T090xC     Puya     Puya.PY32T0xx_DFP   1.0.7     False      
  PY32T092xC     Puya     Puya.PY32T0xx_DFP   1.0.7     False 

```
