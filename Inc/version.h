
#ifndef __VERSION_H__
#define __VERSION_H__


#ifndef DEBUG
# define BOOTLOADER_EN   1 // bootloader presente
# define VER_DEV         0
#else
# define BOOTLOADER_EN   0 // bootloader assente
# define VER_DEV         1
#endif

#define VER_CODE        2225
#define VER_MAJ         0
#define VER_MIN         1
#define VER_PATCH       0


#endif
