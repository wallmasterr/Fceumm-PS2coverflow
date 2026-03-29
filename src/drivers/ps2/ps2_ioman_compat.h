/* Mount modes and stat flags for fileXio; avoids iomanX.h path differences across PS2SDKs. */
#ifndef PS2_IOMAN_COMPAT_H
#define PS2_IOMAN_COMPAT_H

#define FIO_MT_RDONLY 0x0001
#define FIO_MT_RDWR   0x0002

#define FIO_S_IFDIR 0x1000
#define FIO_S_IFREG 0x2000

#endif
