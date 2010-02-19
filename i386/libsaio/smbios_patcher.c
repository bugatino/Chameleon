/*
 * Copyright 2008 mackerintel
 */

#include "libsaio.h"
#include "boot.h"
#include "bootstruct.h"
#include "acpi.h"
#include "efi_tables.h"
#include "fake_efi.h"
#include "platform.h"
#include "smbios_patcher.h"

#ifndef DEBUG_SMBIOS
#define DEBUG_SMBIOS 0
#endif

#if DEBUG_SMBIOS
#define DBG(x...)	printf(x)
#else
#define DBG(x...)
#endif


// defaults for a MacBook
static char sm_macbook_defaults[][2][40]={
	{"SMbiosvendor",	"Apple Inc."			},
	{"SMbiosversion",	"MB41.88Z.0073.B00.0809221748"	},
	{"SMbiosdate",		"04/01/2008"			},
	{"SMmanufacter",	"Apple Inc."			},
	{"SMproductname",	"MacBook4,1"			},
	{"SMsystemversion",	"1.0"				},
	{"SMserial",		"SOMESRLNMBR"			},
	{"SMfamily",		"MacBook"			},
	{"SMboardmanufacter",	"Apple Inc."			},
	{"SMboardproduct",	"Mac-F42D89C8"			},
	{ "",""	}
};

// defaults for a MacBook Pro
static char sm_macbookpro_defaults[][2][40]={
	{"SMbiosvendor",	"Apple Inc."			},
	{"SMbiosversion",	"MBP41.88Z.0073.B00.0809221748"	},
	{"SMbiosdate",		"04/01/2008"			},
	{"SMmanufacter",	"Apple Inc."			},
	{"SMproductname",	"MacBookPro4,1"			},
	{"SMsystemversion",	"1.0"				},
	{"SMserial",		"SOMESRLNMBR"			},
	{"SMfamily",		"MacBookPro"			},
	{"SMboardmanufacter",	"Apple Inc."			},
	{"SMboardproduct",	"Mac-F42D89C8"			},
	{ "",""	}
};

// defaults for a Mac mini 
static char sm_macmini_defaults[][2][40]={
	{"SMbiosvendor",	"Apple Inc."			},
	{"SMbiosversion",	"MM21.88Z.009A.B00.0706281359"	},
	{"SMbiosdate",		"04/01/2008"			},
	{"SMmanufacter",	"Apple Inc."			},
	{"SMproductname",	"Macmini2,1"			},
	{"SMsystemversion",	"1.0"				},
	{"SMserial",		"SOMESRLNMBR"			},
	{"SMfamily",		"Napa Mac"			},
	{"SMboardmanufacter",	"Apple Inc."			},
	{"SMboardproduct",	"Mac-F4208EAA"			},
	{ "",""	}
};

// defaults for an iMac
static char sm_imac_defaults[][2][40]={
	{"SMbiosvendor",	"Apple Inc."			},
	{"SMbiosversion",	"IM81.88Z.00C1.B00.0802091538"	},
	{"SMbiosdate",		"04/01/2008"			},
	{"SMmanufacter",	"Apple Inc."			},
	{"SMproductname",	"iMac8,1"			},	
	{"SMsystemversion",	"1.0"				},
	{"SMserial",		"SOMESRLNMBR"			},
	{"SMfamily",		"Mac"				},
	{"SMboardmanufacter",	"Apple Inc."			},
	{"SMboardproduct",	"Mac-F227BEC8"			},
	{ "",""	}
};

// defaults for a Mac Pro
static char sm_macpro_defaults[][2][40]={
	{"SMbiosvendor",	"Apple Computer, Inc."		},
	{"SMbiosversion",	"MP31.88Z.006C.B05.0802291410"	},
	{"SMbiosdate",		"04/01/2008"			},
	{"SMmanufacter",	"Apple Computer, Inc."		},
	{"SMproductname",	"MacPro3,1"			},
	{"SMsystemversion",	"1.0"				},
	{"SMserial",		"SOMESRLNMBR"			},
	{"SMfamily",		"MacPro"			},
	{"SMboardmanufacter",	"Apple Computer, Inc."		},
	{"SMboardproduct",	"Mac-F4208DC8"			},
	{ "",""	}
};

static char *sm_get_defstr(char *name, int table_num)
{
	int	i;
	char	(*sm_defaults)[2][40];

	if (platformCPUFeature(CPU_FEATURE_MOBILE)) {
		if (Platform.CPU.NoCores > 1) {
			sm_defaults=sm_macbookpro_defaults;
		} else {
			sm_defaults=sm_macbook_defaults;
		}
	} else {
		switch (Platform.CPU.NoCores) {
		case 1: sm_defaults=sm_macmini_defaults; break;
		case 2: sm_defaults=sm_imac_defaults; break;
		default: sm_defaults=sm_macpro_defaults; break;
		}
	}

	for (i=0;sm_defaults[i][0][0];i++) {
		if (!strcmp (sm_defaults[i][0],name)) {
			return sm_defaults[i][1];
		}
	}

	// Shouldn't happen
	printf ("Error: no default for '%s' known\n", name);
	sleep (2);
	return "";
}

static int sm_get_fsb(char *name, int table_num)
{
	return Platform.CPU.FSBFrequency/1000000;
}

static int sm_get_cpu (char *name, int table_num)
{
	return Platform.CPU.CPUFrequency/1000000;
}

static int sm_get_cputype (char *name, int table_num)
{
	if (Platform.CPU.NoCores == 1) {
		return 0x0101;   // <01 01> Intel Core Solo?
	} else if (Platform.CPU.NoCores == 2) {
		return 0x0301;   // <01 03> Intel Core 2 Duo
	} else if (Platform.CPU.NoCores >= 4) {
		return 0x0501;   // <01 05> Quad-Core Intel Xeon
	} else {
		return 0x0301;   // Default to Core 2 Duo
	}
}

static int sm_get_memtype (char *name, int table_num)
{
	int	map;

	if (table_num < MAX_RAM_SLOTS) {
		map = Platform.DMI.DIMM[table_num];
		if (Platform.RAM.DIMM[map].InUse && Platform.RAM.DIMM[map].Type != 0) {
                    DBG("RAM Detected Type = %d\n", Platform.RAM.DIMM[map].Type);
                    return Platform.RAM.DIMM[map].Type;
		}
	}
	return SMB_MEM_TYPE_DDR2;
}

static int sm_get_memspeed (char *name, int table_num)
{
	int	map;

	if (table_num < MAX_RAM_SLOTS) {
		map = Platform.DMI.DIMM[table_num];
		if (Platform.RAM.DIMM[map].InUse && Platform.RAM.DIMM[map].Type != 0) {
                    DBG("RAM Detected Freq = %d Mhz\n", Platform.RAM.DIMM[map].Frequency);
                    return Platform.RAM.DIMM[map].Frequency;
		}
	}

	return 800;
}

static char *sm_get_memvendor (char *name, int table_num)
{
	int	map;

	if (table_num < MAX_RAM_SLOTS) {
		map = Platform.DMI.DIMM[table_num];
		if (Platform.RAM.DIMM[map].InUse && strlen(Platform.RAM.DIMM[map].Vendor) > 0) {
			DBG("RAM Detected Vendor[%d]='%s'\n", table_num, Platform.RAM.DIMM[map].Vendor);
			return Platform.RAM.DIMM[map].Vendor;
		}
	}
	return "N/A";
}
	
static char *sm_get_memserial (char *name, int table_num)
{
	int	map;

	if (table_num < MAX_RAM_SLOTS) {
		map = Platform.DMI.DIMM[table_num];
		if (Platform.RAM.DIMM[map].InUse && strlen(Platform.RAM.DIMM[map].SerialNo) > 0) {
                    DBG("name = %s, map=%d,  RAM Detected SerialNo[%d]='%s'\n", name ? name : "", 
                        map, table_num, Platform.RAM.DIMM[map].SerialNo);
                    return Platform.RAM.DIMM[map].SerialNo;
		}
	}
	return "N/A";
}

static char *sm_get_mempartno (char *name, int table_num)
{
	int	map;

	if (table_num < MAX_RAM_SLOTS) {
		map = Platform.DMI.DIMM[table_num];
		if (Platform.RAM.DIMM[map].InUse && strlen(Platform.RAM.DIMM[map].PartNo) > 0) {
			DBG("Ram Detected PartNo[%d]='%s'\n", table_num, Platform.RAM.DIMM[map].PartNo);
			return Platform.RAM.DIMM[map].PartNo;
		}
	}
	return "N/A";
}

static int sm_one (int tablen)
{
	return 1;
}

struct smbios_property smbios_properties[]=
{
	{.name="SMbiosvendor",		.table_type= 0,	.value_type=SMSTRING,	.offset=0x04,	.auto_str=sm_get_defstr	},
	{.name="SMbiosversion",		.table_type= 0,	.value_type=SMSTRING,	.offset=0x05,	.auto_str=sm_get_defstr	},
	{.name="SMbiosdate",		.table_type= 0,	.value_type=SMSTRING,	.offset=0x08,	.auto_str=sm_get_defstr	},
	{.name="SMmanufacter",		.table_type= 1,	.value_type=SMSTRING,	.offset=0x04,	.auto_str=sm_get_defstr	},
	{.name="SMproductname",		.table_type= 1,	.value_type=SMSTRING,	.offset=0x05,	.auto_str=sm_get_defstr	},
	{.name="SMsystemversion",	.table_type= 1,	.value_type=SMSTRING,	.offset=0x06,	.auto_str=sm_get_defstr	},
	{.name="SMserial",		.table_type= 1,	.value_type=SMSTRING,	.offset=0x07,	.auto_str=sm_get_defstr	},
	{.name="SMUUID",		.table_type= 1, .value_type=SMOWORD,	.offset=0x08,	.auto_oword=0		},
	{.name="SMfamily",		.table_type= 1,	.value_type=SMSTRING,	.offset=0x1a,	.auto_str=sm_get_defstr	},
	{.name="SMboardmanufacter",	.table_type= 2, .value_type=SMSTRING,	.offset=0x04,	.auto_str=sm_get_defstr	},
	{.name="SMboardproduct",	.table_type= 2, .value_type=SMSTRING,	.offset=0x05,	.auto_str=sm_get_defstr	},
	{.name="SMexternalclock",	.table_type= 4,	.value_type=SMWORD,	.offset=0x12,	.auto_int=sm_get_fsb	},
	{.name="SMmaximalclock",	.table_type= 4,	.value_type=SMWORD,	.offset=0x14,	.auto_int=sm_get_cpu	},
	{.name="SMmemdevloc",		.table_type=17,	.value_type=SMSTRING,	.offset=0x10,	.auto_str=0		},
	{.name="SMmembankloc",		.table_type=17,	.value_type=SMSTRING,	.offset=0x11,	.auto_str=0		},
	{.name="SMmemtype",		.table_type=17,	.value_type=SMBYTE,	.offset=0x12,	.auto_int=sm_get_memtype},
	{.name="SMmemspeed",		.table_type=17,	.value_type=SMWORD,	.offset=0x15,	.auto_int=sm_get_memspeed},
	{.name="SMmemmanufacter",	.table_type=17,	.value_type=SMSTRING,	.offset=0x17,	.auto_str=sm_get_memvendor},
	{.name="SMmemserial",		.table_type=17,	.value_type=SMSTRING,	.offset=0x18,	.auto_str=sm_get_memserial},
	{.name="SMmempart",		.table_type=17,	.value_type=SMSTRING,	.offset=0x1A,	.auto_str=sm_get_mempartno},
	{.name="SMcputype",		.table_type=131,.value_type=SMWORD,	.offset=0x04,	.auto_int=sm_get_cputype},
	{.name="SMbusspeed",		.table_type=132,.value_type=SMWORD,	.offset=0x04,	.auto_str=0		}
};

struct smbios_table_description smbios_table_descriptions[]=
{
	{.type=0,	.len=0x18,	.numfunc=sm_one},
	{.type=1,	.len=0x1b,	.numfunc=sm_one},
	{.type=2,	.len=0x0f,	.numfunc=sm_one},
	{.type=4,	.len=0x2a,	.numfunc=sm_one},
	{.type=17,	.len=0x1c,	.numfunc=0},
	{.type=131,	.len=0x06,	.numfunc=sm_one},
	{.type=132,	.len=0x06,	.numfunc=sm_one}
};

// getting smbios addr with fast compare ops, late checksum testing ...
#define COMPARE_DWORD(a,b) ( *((u_int32_t *) a) == *((u_int32_t *) b) )
static const char * const SMTAG = "_SM_";
static const char* const DMITAG= "_DMI_";

static struct SMBEntryPoint *getAddressOfSmbiosTable(void)
{
	struct SMBEntryPoint	*smbios;

	/* 
	 * The logic is to start at 0xf0000 and end at 0xfffff iterating 16 bytes at a time looking
	 * for the SMBIOS entry-point structure anchor (literal ASCII "_SM_").
	 */
	smbios = (struct SMBEntryPoint*) SMBIOS_RANGE_START;
	while (smbios <= (struct SMBEntryPoint *)SMBIOS_RANGE_END) {
            if (COMPARE_DWORD(smbios->anchor, SMTAG)  && COMPARE_DWORD(smbios->dmi.anchor, DMITAG) &&
		    checksum8(smbios, sizeof(struct SMBEntryPoint)) == 0)
	    {
                return smbios;
	    }
            smbios = (((void*) smbios) + 16);
	}
	printf("ERROR: Unable to find SMBIOS!\n");
	sleep(5);
	return NULL;
}

/** Compute necessary space requirements for new smbios */
struct SMBEntryPoint *smbios_dry_run(struct SMBEntryPoint *origsmbios)
{
	struct SMBEntryPoint	*ret;
	char			*smbiostables;
	char			*tablesptr;
	int			origsmbiosnum;
	int			i, j;
	int			tablespresent[256];
	bool			do_auto=true;

	bzero(tablespresent, sizeof(tablespresent));

	getBoolForKey(kSMBIOSdefaults, &do_auto, &bootInfo->bootConfig);

	ret = (struct SMBEntryPoint *)AllocateKernelMemory(sizeof(struct SMBEntryPoint));
	if (origsmbios) {
		smbiostables = (char *)origsmbios->dmi.tableAddress;
		origsmbiosnum = origsmbios->dmi.structureCount;
	} else {
		smbiostables = NULL;
		origsmbiosnum = 0;
	}

	// _SM_
	ret->anchor[0] = 0x5f;
	ret->anchor[1] = 0x53;
	ret->anchor[2] = 0x4d;
	ret->anchor[3] = 0x5f; 
	ret->entryPointLength = sizeof(*ret);
	ret->majorVersion = 2;
	ret->minorVersion = 1;
	ret->maxStructureSize = 0; // will be calculated later in this function
	ret->entryPointRevision = 0;
	for (i=0;i<5;i++) {
		ret->formattedArea[i] = 0;
	}
	//_DMI_
	ret->dmi.anchor[0] = 0x5f;
	ret->dmi.anchor[1] = 0x44;
	ret->dmi.anchor[2] = 0x4d;
	ret->dmi.anchor[3] = 0x49;
	ret->dmi.anchor[4] = 0x5f;
	ret->dmi.tableLength = 0;  // will be calculated later in this function
	ret->dmi.tableAddress = 0; // will be initialized in smbios_real_run()
	ret->dmi.structureCount = 0; // will be calculated later in this function
	ret->dmi.bcdRevision = 0x21;
	tablesptr = smbiostables;

        // add stringlen of overrides to original stringlen, update maxStructure size adequately, 
        // update structure count and tablepresent[type] with count of type. 
	if (smbiostables) {
		for (i=0; i<origsmbiosnum; i++) {
			struct smbios_table_header	*cur = (struct smbios_table_header *)tablesptr;
			char				*stringsptr;
			int				stringlen;

			tablesptr += cur->length;
			stringsptr = tablesptr;
			for (; tablesptr[0]!=0 || tablesptr[1]!=0; tablesptr++);
			tablesptr += 2;
			stringlen = tablesptr - stringsptr - 1;
			if (stringlen == 1) {
				stringlen = 0;
			}
			for (j=0; j<sizeof(smbios_properties)/sizeof(smbios_properties[0]); j++) {
				const char	*str;
				int		size;
				char		altname[40];

				sprintf(altname, "%s_%d",smbios_properties[j].name, tablespresent[cur->type] + 1);				
				if (smbios_properties[j].table_type == cur->type &&
				    smbios_properties[j].value_type == SMSTRING &&
				    (getValueForKey(smbios_properties[j].name, &str, &size, &bootInfo->smbiosConfig) ||
				     getValueForKey(altname,&str, &size, &bootInfo->smbiosConfig)))
				{
					stringlen += size + 1;
				} else if (smbios_properties[j].table_type == cur->type &&
				           smbios_properties[j].value_type == SMSTRING &&
				           do_auto && smbios_properties[j].auto_str)
				{
					stringlen += strlen(smbios_properties[j].auto_str(smbios_properties[j].name, tablespresent[cur->type])) + 1;
				}
			}
			if (stringlen == 0) {
				stringlen = 1;
			}
			stringlen++;
			if (ret->maxStructureSize < cur->length+stringlen) {
				ret->maxStructureSize=cur->length+stringlen;
			}
			ret->dmi.tableLength += cur->length+stringlen;
			ret->dmi.structureCount++;
			tablespresent[cur->type]++;
		}
	}
        // Add eventually table types whose detected count would be < required count, and update ret header with:
        // new stringlen addons, structure count, and tablepresent[type] count adequately
	for (i=0; i<sizeof(smbios_table_descriptions)/sizeof(smbios_table_descriptions[0]); i++) {
		int	numnec=-1;
		char	buffer[40];

		sprintf(buffer, "SMtable%d", i);
		if (!getIntForKey(buffer, &numnec, &bootInfo->smbiosConfig)) {
			numnec = -1;
		}
		if (numnec==-1 && do_auto && smbios_table_descriptions[i].numfunc) {
			numnec = smbios_table_descriptions[i].numfunc(smbios_table_descriptions[i].type);
		}
		while (tablespresent[smbios_table_descriptions[i].type] < numnec) {
			int	stringlen = 0;
			for (j=0; j<sizeof(smbios_properties)/sizeof(smbios_properties[0]); j++) {
				const char	*str;
				int		size;
				char		altname[40];

				sprintf(altname, "%s_%d",smbios_properties[j].name, tablespresent[smbios_table_descriptions[i].type] + 1);
				if (smbios_properties[j].table_type == smbios_table_descriptions[i].type &&
				    smbios_properties[j].value_type == SMSTRING &&
				    (getValueForKey(altname, &str, &size, &bootInfo->smbiosConfig) ||
				     getValueForKey(smbios_properties[j].name, &str, &size, &bootInfo->smbiosConfig)))
				{
					stringlen += size + 1;
				} else if (smbios_properties[j].table_type == smbios_table_descriptions[i].type &&
				           smbios_properties[j].value_type==SMSTRING &&
				           do_auto && smbios_properties[j].auto_str)
				{
					stringlen += strlen(smbios_properties[j].auto_str(smbios_properties[j].name, tablespresent[smbios_table_descriptions[i].type])) + 1;
				}
			}
			if (stringlen == 0) {
				stringlen = 1;
			}
			stringlen++;
			if (ret->maxStructureSize < smbios_table_descriptions[i].len+stringlen) {
				ret->maxStructureSize = smbios_table_descriptions[i].len + stringlen;
			}
			ret->dmi.tableLength += smbios_table_descriptions[i].len + stringlen;
			ret->dmi.structureCount++;
			tablespresent[smbios_table_descriptions[i].type]++;
		}
	}
	return ret;
}

/** From the origsmbios detected by getAddressOfSmbiosTable() to newsmbios whose entrypoint 
 * struct has been created by smbios_dry_run, update each table struct content of new smbios
 * int the new allocated table address of size newsmbios->tablelength.
 */
void smbios_real_run(struct SMBEntryPoint * origsmbios, struct SMBEntryPoint * newsmbios)
{
	char *smbiostables;
	char *tablesptr, *newtablesptr;
	int origsmbiosnum;
	// bitmask of used handles
	uint8_t handles[8192]; 
	uint16_t nexthandle=0;
	int i, j;
	int tablespresent[256];
	bool do_auto=true;
        
        extern void dumpPhysAddr(const char * title, void * a, int len);

	bzero(tablespresent, sizeof(tablespresent));
	bzero(handles, sizeof(handles));

	getBoolForKey(kSMBIOSdefaults, &do_auto, &bootInfo->bootConfig);
	
	newsmbios->dmi.tableAddress = (uint32_t)AllocateKernelMemory(newsmbios->dmi.tableLength);
	if (origsmbios) {
		smbiostables = (char *)origsmbios->dmi.tableAddress;
		origsmbiosnum = origsmbios->dmi.structureCount;
	} else {
		smbiostables = NULL;
		origsmbiosnum = 0;
	}
	tablesptr = smbiostables;
	newtablesptr = (char *)newsmbios->dmi.tableAddress;

        // if old smbios exists then update new smbios  with old smbios original content first
	if (smbiostables) {
		for (i=0; i<origsmbiosnum; i++) {
			struct smbios_table_header	*oldcur = (struct smbios_table_header *) tablesptr;
			struct smbios_table_header	*newcur = (struct smbios_table_header *) newtablesptr;
			char				*stringsptr;
			int				nstrings = 0;

			handles[(oldcur->handle) / 8] |= 1 << ((oldcur->handle) % 8);

                        // copy table length from old table to new table but not the old strings
			memcpy(newcur,oldcur, oldcur->length);

			tablesptr += oldcur->length;
			stringsptr = tablesptr;
			newtablesptr += oldcur->length;

                        // calculate the number of strings in the old content
			for (;tablesptr[0]!=0 || tablesptr[1]!=0; tablesptr++) {
				if (tablesptr[0] == 0) {
					nstrings++;
				}
			}
			if (tablesptr != stringsptr) {
				nstrings++;
			}
			tablesptr += 2;

                        // copy the old strings to new table
			memcpy(newtablesptr, stringsptr, tablesptr-stringsptr);

 #if 0
                        // DEBUG: display this original table 17
                        
                        if (oldcur->type==6 || oldcur->type==17)
                        {
                            dumpPhysAddr("orig table:", oldcur, oldcur->length + ( tablesptr-stringsptr));
                        }
#endif
			// point to next possible space for a string (deducting the second 0 char at the end)
			newtablesptr += tablesptr - stringsptr - 1;
                            if (nstrings == 0) { // if no string was found rewind to the first 0 char of the 0,0 terminator
				newtablesptr--;
			}

                        // now for each property in the table update the overrides if any (auto or user)
			for (j=0; j<sizeof(smbios_properties)/sizeof(smbios_properties[0]); j++) {
				const char	*str;
				int		size;
				int		num;
				char		altname[40];

				sprintf(altname, "%s_%d", smbios_properties[j].name, tablespresent[newcur->type] + 1);
				if (smbios_properties[j].table_type == newcur->type) {
					switch (smbios_properties[j].value_type) {
					case SMSTRING:
						if (getValueForKey(altname, &str, &size, &bootInfo->smbiosConfig) ||
						    getValueForKey(smbios_properties[j].name, &str, &size, &bootInfo->smbiosConfig))
						{
							memcpy(newtablesptr, str, size);
							newtablesptr[size] = 0;
							newtablesptr += size + 1;
							*((uint8_t*)(((char*)newcur) + smbios_properties[j].offset)) = ++nstrings;
						} else if (do_auto && smbios_properties[j].auto_str) {
							str = smbios_properties[j].auto_str(smbios_properties[j].name, tablespresent[newcur->type]);
							size = strlen(str);
							memcpy(newtablesptr, str, size);
							newtablesptr[size] = 0;
							newtablesptr += size + 1;
							*((uint8_t*)(((char*)newcur) + smbios_properties[j].offset)) = ++nstrings;
						}
						break;

					case SMOWORD:
						if (getValueForKey(altname, &str, &size, &bootInfo->smbiosConfig) ||
						    getValueForKey(smbios_properties[j].name, &str, &size, &bootInfo->smbiosConfig))
						{
							int		k=0, t=0, kk=0;
							const char	*ptr = str;
							memset(((char*)newcur) + smbios_properties[j].offset, 0, 16);
							while (ptr-str<size && *ptr && (*ptr==' ' || *ptr=='\t' || *ptr=='\n')) {
								ptr++;
							}
							if (size-(ptr-str)>=2 && ptr[0]=='0' && (ptr[1]=='x' || ptr[1]=='X')) {
								ptr += 2;
							}
							for (;ptr-str<size && *ptr && k<16;ptr++) {
								if (*ptr>='0' && *ptr<='9') {
									(t=(t<<4)|(*ptr-'0')),kk++;
								}
								if (*ptr>='a' && *ptr<='f') {
									(t=(t<<4)|(*ptr-'a'+10)),kk++;
								}
								if (*ptr>='A' && *ptr<='F') {
									(t=(t<<4)|(*ptr-'A'+10)),kk++;
								}
								if (kk == 2) {
									*((uint8_t*)(((char*)newcur) + smbios_properties[j].offset + k)) = t;
									k++;
									kk = 0;
									t = 0;
								}
							}
						}
						break;

					case SMBYTE:
						if (getIntForKey(altname, &num, &bootInfo->smbiosConfig) ||
						    getIntForKey(smbios_properties[j].name, &num, &bootInfo->smbiosConfig))
						{
							*((uint8_t*)(((char*)newcur) + smbios_properties[j].offset)) = num;
						} else if (do_auto && smbios_properties[j].auto_int) {
							*((uint8_t*)(((char*)newcur) + smbios_properties[j].offset)) = smbios_properties[j].auto_int(smbios_properties[j].name, tablespresent[newcur->type]);							
						}
						break;

					case SMWORD:
						if (getIntForKey(altname, &num, &bootInfo->smbiosConfig) ||
						    getIntForKey(smbios_properties[j].name, &num, &bootInfo->smbiosConfig))
						{
							*((uint16_t*)(((char*)newcur) + smbios_properties[j].offset)) = num;
						} else if (do_auto && smbios_properties[j].auto_int) {
							*((uint16_t*)(((char*)newcur) + smbios_properties[j].offset)) = smbios_properties[j].auto_int(smbios_properties[j].name, tablespresent[newcur->type]);
						}
						break;
					}
				}
			}
			if (nstrings == 0) {
				newtablesptr[0] = 0;
				newtablesptr++;
			}
			newtablesptr[0] = 0;
			newtablesptr++;
			tablespresent[newcur->type]++;
		}
	}

        // for each eventual complementary table not present in the original smbios, do the overrides
	for (i=0; i<sizeof(smbios_table_descriptions)/sizeof(smbios_table_descriptions[0]); i++) {
		int	numnec = -1;
		char	buffer[40];

		sprintf(buffer, "SMtable%d", i);
		if (!getIntForKey(buffer, &numnec, &bootInfo->smbiosConfig)) {
			numnec = -1;
		}
		if (numnec == -1 && do_auto && smbios_table_descriptions[i].numfunc) {
			numnec = smbios_table_descriptions[i].numfunc(smbios_table_descriptions[i].type);
		}
		while (tablespresent[smbios_table_descriptions[i].type] < numnec) {
			struct smbios_table_header	*newcur = (struct smbios_table_header *) newtablesptr;
			int				nstrings = 0;

			memset(newcur,0, smbios_table_descriptions[i].len);
			while (handles[(nexthandle)/8] & (1 << ((nexthandle) % 8))) {
				nexthandle++;
			}
			newcur->handle = nexthandle;
			handles[nexthandle / 8] |= 1 << (nexthandle % 8);
			newcur->type = smbios_table_descriptions[i].type;
			newcur->length = smbios_table_descriptions[i].len;
			newtablesptr += smbios_table_descriptions[i].len;
			for (j=0; j<sizeof(smbios_properties)/sizeof(smbios_properties[0]); j++) {
				const char	*str;
				int		size;
				int		num;
				char		altname[40];

				sprintf(altname, "%s_%d", smbios_properties[j].name, tablespresent[newcur->type] + 1);
				if (smbios_properties[j].table_type == newcur->type) {
					switch (smbios_properties[j].value_type) {
					case SMSTRING:
						if (getValueForKey(altname, &str, &size, &bootInfo->smbiosConfig) ||
						    getValueForKey(smbios_properties[j].name, &str, &size, &bootInfo->smbiosConfig))
						{
							memcpy(newtablesptr, str, size);
							newtablesptr[size] = 0;
							newtablesptr += size + 1;
							*((uint8_t*)(((char*)newcur) + smbios_properties[j].offset)) = ++nstrings;
						} else if (do_auto && smbios_properties[j].auto_str) {
							str = smbios_properties[j].auto_str(smbios_properties[j].name, tablespresent[newcur->type]);
							size = strlen(str);
							memcpy(newtablesptr, str, size);
							newtablesptr[size] = 0;
							newtablesptr += size + 1;
							*((uint8_t*)(((char*)newcur) + smbios_properties[j].offset)) = ++nstrings;
						}
						break;

					case SMOWORD:
						if (getValueForKey(altname, &str, &size, &bootInfo->smbiosConfig) ||
						    getValueForKey(smbios_properties[j].name, &str, &size, &bootInfo->smbiosConfig))
						{
							int		k=0, t=0, kk=0;
							const char	*ptr = str;

							memset(((char*)newcur) + smbios_properties[j].offset, 0, 16);
							while (ptr-str<size && *ptr && (*ptr==' ' || *ptr=='\t' || *ptr=='\n')) {
								ptr++;
							}
							if (size-(ptr-str)>=2 && ptr[0]=='0' && (ptr[1]=='x' || ptr[1]=='X')) {
								ptr += 2;
							}
							for (;ptr-str<size && *ptr && k<16;ptr++) {
								if (*ptr>='0' && *ptr<='9') {
									(t=(t<<4)|(*ptr-'0')),kk++;
								}
								if (*ptr>='a' && *ptr<='f') {
									(t=(t<<4)|(*ptr-'a'+10)),kk++;
								}
								if (*ptr>='A' && *ptr<='F') {
									(t=(t<<4)|(*ptr-'A'+10)),kk++;
								}
								if (kk == 2) {
									*((uint8_t*)(((char*)newcur) + smbios_properties[j].offset + k)) = t;
									k++;
									kk = 0;
									t = 0;
								}
							}
						}
						break;
						
					case SMBYTE:
						if (getIntForKey(altname, &num, &bootInfo->smbiosConfig) ||
						    getIntForKey(smbios_properties[j].name, &num, &bootInfo->smbiosConfig))
						{
							*((uint8_t*)(((char*)newcur) + smbios_properties[j].offset)) = num;
						} else if (do_auto && smbios_properties[j].auto_int) {
							*((uint8_t*)(((char*)newcur) + smbios_properties[j].offset)) = smbios_properties[j].auto_int(smbios_properties[j].name, tablespresent[newcur->type]);
						}
						break;
						
					case SMWORD:
						if (getIntForKey(altname, &num, &bootInfo->smbiosConfig) ||
						    getIntForKey(smbios_properties[j].name, &num, &bootInfo->smbiosConfig))
						{
							*((uint16_t*)(((char*)newcur) + smbios_properties[j].offset)) = num;
						} else if (do_auto && smbios_properties[j].auto_int) {
							*((uint16_t*)(((char*)newcur)+smbios_properties[j].offset)) = smbios_properties[j].auto_int(smbios_properties[j].name, tablespresent[newcur->type]);
						}
						break;
					}
				}
			}
			if (nstrings == 0) {
				newtablesptr[0] = 0;
				newtablesptr++;
			}
			newtablesptr[0] = 0;
			newtablesptr++;
			tablespresent[smbios_table_descriptions[i].type]++;
		}
	}

        // calculate new checksums
	newsmbios->dmi.checksum = 0;
	newsmbios->dmi.checksum = 256 - checksum8(&newsmbios->dmi, sizeof(newsmbios->dmi));
	newsmbios->checksum = 0;
	newsmbios->checksum = 256 - checksum8(newsmbios, sizeof(*newsmbios));
	verbose("Patched DMI Table\n");
}

struct SMBEntryPoint *getSmbios(int which)
{
    static struct SMBEntryPoint *orig = NULL; // cached
    static struct SMBEntryPoint *patched = NULL; // cached

	if (orig == NULL) orig = getAddressOfSmbiosTable();

	switch (which) {
	case SMBIOS_ORIGINAL:
            return orig;
	case SMBIOS_PATCHED:
            if (patched == NULL) {
                if (orig==NULL)  orig = getAddressOfSmbiosTable();
                patched = smbios_dry_run(orig);
                smbios_real_run(orig, patched);
            }
            return patched;
	default:
            printf("ERROR: invalid option for getSmbios() !!\n");
            return NULL;
	}
}

/** 
 * Get a table structure entry from a type specification and a smbios address
 * return NULL if table is not found
 */
struct DMIHeader *getSmbiosTableStructure(struct SMBEntryPoint	*smbios, int type, int min_length)
{
    struct DMIHeader * dmihdr=NULL;
    bool found = false;
    SMBByte* p;
    int i;

    if (smbios == NULL || type < 0 ) return NULL;
#if DEBUG_SMBIOS
    printf(">>> SMBIOSAddr=0x%08x\n", smbios);
    printf(">>> DMI: addr=0x%08x, len=%d, count=%d\n", smbios->dmi.tableAddress, 
           smbios->dmi.tableLength, smbios->dmi.structureCount);
#endif
    p = (SMBByte *) smbios->dmi.tableAddress;
    for (i=0; i < smbios->dmi.structureCount && p + 4 <= (SMBByte *)smbios->dmi.tableAddress + smbios->dmi.tableLength; i++) 
	{
            dmihdr = (struct DMIHeader *) p;
#if DEBUG_SMBIOS
            // verbose(">>>>>> DMI(%d): type=0x%02x, len=0x%d\n",i,dmihdr->type,dmihdr->length);
#endif
            if (dmihdr->length < 4 || dmihdr->type == 127 /* EOT */) break;
            if (dmihdr->type == type) 	/* 3.3.2 System Information */
            {	
                if (dmihdr->length >= min_length) found = true;
                break;
	    }
            p = p + dmihdr->length;
            while ((p - (SMBByte *)smbios->dmi.tableAddress + 1 < smbios->dmi.tableLength) && (p[0] != 0x00 || p[1] != 0x00)) 
	    {
                    p++;
	    }
            p += 2;
	}

#if DEBUG_SMBIOS
        printf("DMI header found for table type %d, length = %d\n", type, dmihdr->type, dmihdr->length);
#endif
	return found ? dmihdr : NULL;
 }
