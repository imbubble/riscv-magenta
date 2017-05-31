/*
 * This file was generated by the mkbuiltins program.
 */

#define ALIASCMD (builtincmd + 3)
#define BREAKCMD (builtincmd + 4)
#define CDCMD (builtincmd + 5)
#define COMMANDCMD (builtincmd + 7)
#define DOTCMD (builtincmd + 0)
#define ECHOCMD (builtincmd + 12)
#define EVALCMD (builtincmd + 13)
#define EXECCMD (builtincmd + 14)
#define EXITCMD (builtincmd + 15)
#define EXPORTCMD (builtincmd + 16)
#define FALSECMD (builtincmd + 17)
#define GETOPTSCMD (builtincmd + 18)
#define HASHCMD (builtincmd + 19)
#define JOBSCMD (builtincmd + 20)
#define LOCALCMD (builtincmd + 23)
#define MXC_DM (builtincmd + 10)
#define MXC_DUMP (builtincmd + 11)
#define MXC_K (builtincmd + 21)
#define MXC_LIST (builtincmd + 22)
#define MXC_LS (builtincmd + 24)
#define MXC_MKDIR (builtincmd + 25)
#define MXC_MSLEEP (builtincmd + 26)
#define MXC_MV_OR_CP (builtincmd + 9)
#define MXC_RM (builtincmd + 33)
#define PRINTFCMD (builtincmd + 28)
#define PWDCMD (builtincmd + 29)
#define READCMD (builtincmd + 30)
#define RETURNCMD (builtincmd + 32)
#define SETCMD (builtincmd + 34)
#define SHIFTCMD (builtincmd + 35)
#define TESTCMD (builtincmd + 2)
#define TIMESCMD (builtincmd + 37)
#define TRAPCMD (builtincmd + 38)
#define TRUECMD (builtincmd + 1)
#define TYPECMD (builtincmd + 40)
#define UMASKCMD (builtincmd + 41)
#define UNALIASCMD (builtincmd + 42)
#define UNSETCMD (builtincmd + 43)
#define WAITCMD (builtincmd + 44)

#define NUMBUILTINS 45

#define BUILTIN_SPECIAL 0x1
#define BUILTIN_REGULAR 0x2
#define BUILTIN_ASSIGN 0x4
#define BUILTIN_WEAK 0x8

struct builtincmd {
	const char *name;
	int (*builtin)(int, char **);
	unsigned flags;
};

extern const struct builtincmd builtincmd[];
