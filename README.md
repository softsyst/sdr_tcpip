# sdr_tcpip
## rtl_tcp
**TCP/IP Server for I/Q data delivered by RTL-SDR dongles**
- The rtl_tcp server, based on the well-known software of **osmocom**, has significantly been enhanced and is continuously being improved by [rundfunkforum](https://www.rundfunkforum.de/viewforum.php?f=11) user **Oldenburger**, aka on GitHub **old-dab**.
- It is strongly recommended NOT to use the files of this repository here, as they are - possibly not the latest - copies of the ones of GitHub user old-dab.
Please use [old-dab's](https://github.com/old-dab/rtlsdr) files to have always the latest version of rtl_tcp.

## airspy_tcp
**TCP/IP Server for I/Q data delivered by Airspy devices**
- Based on the [airspy_tcp](https://github.com/TLeconte/airspy_tcp) software of **Thierry Leconte**
- 8- and 16-Bit I/Q data selectable
- BiasT
- Binary is included in the QIRX download package.
### Version 0.14
Fractional ppm frequency correction.
### Version 0.13
Change of the conversion 16Bit->8Bit.
The 16Bit samples as delivered from the libairspy.dll are first converted to their native 12Bit format, and then have their least four bits discarded.
- Please note that V0.13 is NOT compatible with QIRX V2.x, only with QIRX V3.x.
- With QIRX V2.x, please continue to use the V0.12, as contained as binary in that QIRX package.

## Build
### Windows
- Solution for VS2017, to build 64-Bit versions. Other versions have not been tested.
