#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ytnef.h>
#include "config.h"

#define PRODID "PRODID:-//The Gauntlet//" PACKAGE_STRING "//EN\n"

int verbose = 0;
int savefiles = 0;
int saveRTF = 0;
int saveintermediate = 0;
char *filepath = NULL;
void ProcessTNEF(TNEFStruct TNEF);
void SaveVCalendar(TNEFStruct TNEF);
void SaveVCard(TNEFStruct TNEF);
void SaveVTask(TNEFStruct TNEF);
unsigned char * DecompressRTF(variableLength *p, int *size);


void PrintHelp(void) {
    printf("Yerase TNEF Exporter v");
            printf(VERSION);
            printf("\n");
    printf("\n");
    printf("  usage: ytnef [-+vhf] <filenames>\n");
    printf("\n");
    printf("   -/+v - Enables/Disables verbose output\n");
    printf("          Multiple -v's increase the level of output\n");
    printf("   -/+f - Enables/Disables saving of attachments\n");
    printf("   -/+F - Enables/Disables saving of the message body as RTF\n");
    printf("   -/+a - Enables/Disables saving of intermediate files\n");
    printf("   -h   - Displays this help message\n");
    printf("\n");
    printf("Example:\n");
    printf("  ytnef -v winmail.dat\n");
    printf("     Parse with verbose output, don't save\n");
    printf("  ytnef -f . winmail.dat\n");
    printf("     Parse and save all attachments to local directory (.)\n");
    printf("  ytnef -F -f . winmail.dat\n");
    printf("     Parse and save all attachments to local directory (.)\n");
    printf("     Including saving the message text to a RTF file.\n\n");
    printf("Send bug reports to ");
        printf(PACKAGE_BUGREPORT);
        printf("\n");

}


int main(int argc, char ** argv) {
    int index,i;
    TNEFStruct TNEF;

//    printf("Size of WORD is %i\n", sizeof(WORD));
//    printf("Size of DWORD is %i\n", sizeof(DWORD));
//    printf("Size of DDWORD is %i\n", sizeof(DDWORD));

    if (argc == 1) {
        printf("You must specify files to parse\n");
        PrintHelp();
        return -1;
    }
    
    for(i=1; i<argc; i++) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
                case 'a': saveintermediate = 1;
                          break;
                case 'v': verbose++;
                          break;
                case 'h': PrintHelp();
                          return;
                case 'f': savefiles = 1;
                          filepath = argv[i+1];
                          i++;
                          break;
                case 'F': saveRTF = 1;
                          break;
                default: 
                          printf("Unknown option '%s'\n", argv[i]);
            }
            continue;

        }
        if (argv[i][0] == '+') {
            switch (argv[i][1]) {
                case 'a': saveintermediate = 0;
                          break;
                case 'v': verbose--;
                          break;
                case 'f': savefiles = 0;
                          filepath = NULL;
                          break;
                case 'F': saveRTF = 0;
                          break;
                default: 
                          printf("Unknown option '%s'\n", argv[i]);
            }
            continue;

        }

        TNEFInitialize(&TNEF);
        TNEF.Debug = verbose;
        if (TNEFParseFile(argv[i], &TNEF) == -1) {
            printf("ERROR processing file\n");
            continue;
        }
        ProcessTNEF(TNEF);
        TNEFFree(&TNEF);
    }
}

void ProcessTNEF(TNEFStruct TNEF) {
    char *astring;
    variableLength *filename;
    variableLength *filedata;
    Attachment *p;
    int RealAttachment;
    int object;
    char ifilename[256];
    int i;
    int foundCal=0;

    FILE *fptr;

// First see if this requires special processing.
// ie: it's a Contact Card, Task, or Meeting request (vCal/vCard)
    if (TNEF.messageClass[0] != 0)  {
        if (strcmp(TNEF.messageClass, "IPM.Contact") == 0) {
            SaveVCard(TNEF );
        }
        if (strcmp(TNEF.messageClass, "IPM.Task") == 0) {
            SaveVTask(TNEF);
        }
        if (strcmp(TNEF.messageClass, "IPM.Appointment") == 0) {
            SaveVCalendar(TNEF);
            foundCal = 1;
        }
    }
    if ((filename = MAPIFindUserProp(&(TNEF.MapiProperties), 
                        PROP_TAG(PT_STRING8,0x24))) != MAPI_UNDEFINED) {
        if (strcmp(filename->data, "IPM.Appointment") == 0) {
            // If it's "indicated" twice, we don't want to save 2 calendar
            // entries.
            if (foundCal == 0) {
                SaveVCalendar(TNEF);
            }
        }
    }
    if ((saveRTF == 1) && (filedata=MAPIFindProperty(&(TNEF.MapiProperties),
                            PROP_TAG(PT_BINARY, PR_RTF_COMPRESSED)))
            != MAPI_UNDEFINED) {
        int size;
        char *buf;
        if ((buf = DecompressRTF(filedata, &size)) != NULL) {
            printf("%i bytes\n", size);
            for (i=0; i< size; i++) {
                printf("%c", buf[i]);
            }
            printf("\n");
            free(buf);
        } else {
            printf("Error");
        }
        
    } 

// Now process each attachment
    p = TNEF.starting_attach.next;
    while (p != NULL) {
        // Make sure it has a size.
        if (p->FileData.size > 0) {
            object = 1;           

            
            // See if the contents are stored as "attached data"
            //  Inside the MAPI blocks.
            if((filedata = MAPIFindProperty(&(p->MAPI), 
                                    PROP_TAG(PT_OBJECT, PR_ATTACH_DATA_OBJ))) 
                    == MAPI_UNDEFINED) {
                if((filedata = MAPIFindProperty(&(p->MAPI), 
                                    PROP_TAG(PT_BINARY, PR_ATTACH_DATA_OBJ))) 
                        == MAPI_UNDEFINED) {
                    // Nope, standard TNEF stuff.
                    filedata = &(p->FileData);
                    object = 0;
                }
            }
            // See if this is an embedded TNEF stream.
            RealAttachment = 1;
            if (object == 1) {
                // This is an "embedded object", so skip the
                // 16-byte identifier first.
                TNEFStruct emb_tnef;
                DWORD signature;
                memcpy(&signature, filedata->data+16, sizeof(DWORD));
                if (TNEFCheckForSignature(signature) == 0) {
                    // Has a TNEF signature, so process it.
                    TNEFInitialize(&emb_tnef);
                    emb_tnef.Debug = TNEF.Debug;
                    if (TNEFParseMemory(filedata->data+16, 
                            filedata->size-16, &emb_tnef) != -1) {
                        ProcessTNEF(emb_tnef);
                        RealAttachment = 0;
                    }
                    TNEFFree(&emb_tnef);
                }
            } else {
                TNEFStruct emb_tnef;
                DWORD signature;
                memcpy(&signature, filedata->data, sizeof(DWORD));
                if (TNEFCheckForSignature(signature) == 0) {
                    // Has a TNEF signature, so process it.
                    TNEFInitialize(&emb_tnef);
                    emb_tnef.Debug = TNEF.Debug;
                    if (TNEFParseMemory(filedata->data, 
                            filedata->size, &emb_tnef) != -1) {
                        ProcessTNEF(emb_tnef);
                        RealAttachment = 0;
                    }
                    TNEFFree(&emb_tnef);
                }
            }
            if ((RealAttachment == 1) || (saveintermediate == 1)) {
                // Ok, it's not an embedded stream, so now we
                // process it.
                if ((filename = MAPIFindProperty(&(p->MAPI), 
                                        PROP_TAG(30,0x3707))) 
                        == MAPI_UNDEFINED) {
                    if ((filename = MAPIFindProperty(&(p->MAPI), 
                                        PROP_TAG(30,0x3001))) 
                            == MAPI_UNDEFINED) {
                        filename = &(p->Title);
                    }
                }
                if (filepath == NULL) {
                    sprintf(ifilename, "%s", filename->data);
                } else {
                    sprintf(ifilename, "%s/%s", filepath, filename->data);
                }
                for(i=0; i<strlen(ifilename); i++) 
                    if (ifilename[i] == ' ') 
                        ifilename[i] = '_';
                printf("%s\n", ifilename);
                if (savefiles == 1) {
                    if ((fptr = fopen(ifilename, "wb"))==NULL) {
                        printf("ERROR: Error writing file to disk!");
                    } else {
                        if (object == 1) {
                            fwrite(filedata->data + 16, 
                                    sizeof(BYTE), 
                                    filedata->size - 16, 
                                    fptr);
                        } else {
                            fwrite(filedata->data, 
                                    sizeof(BYTE), 
                                    filedata->size, 
                                    fptr);
                        }
                        fclose(fptr);
                    } // if we opened successfully
                } // if savefiles == 1
            } // if RealAttachment == 1
        } // if size>0
        p=p->next;
    } // while p!= null
}

#define RTF_PREBUF "{\\rtf1\\ansi\\mac\\deff0\\deftab720{\\fonttbl;}{\\f0\\fnil \\froman \\fswiss \\fmodern \\fscript \\fdecor MS Sans SerifSymbolArialTimes New RomanCourier{\\colortbl\\red0\\green0\\blue0\n\r\\par \\pard\\plain\\f0\\fs20\\b\\i\\u\\tab\\tx"

unsigned char *DecompressRTF(variableLength *p, int *size) {
    unsigned char *dst; // destination for uncompressed bytes
    unsigned char *src;
    unsigned int in;
    unsigned int out;
    int i;
    variableLength comp_Prebuf;

    comp_Prebuf.size = strlen(RTF_PREBUF);
    comp_Prebuf.data = calloc(comp_Prebuf.size, 1);
    strcpy(comp_Prebuf.data, RTF_PREBUF);

    printf("Found an RTF Stream: %i bytes\n", p->size);

    src = p->data;
    in = 0;

    ULONG compressedSize = (ULONG)SwapDWord(src+in);
    in += 4;
    ULONG uncompressedSize = (ULONG)SwapDWord(src+in);
    in += 4;
    DWORD magic = SwapDWord(src+in);
    in += 4;
    DWORD crc32 = SwapDWord(src+in);
    in += 4;

    printf(" compressedSize = %i\n", compressedSize);
    printf(" uncompressedSize = %i\n", uncompressedSize);
    printf(" magic = %x\n", magic);
    printf(" crc32 = %x\n", crc32);
    // check size excluding the size field itself
    if (compressedSize != p->size - 4) {
        printf(" Size Mismatch: %i != %i\n", compressedSize, p->size-4);
        return NULL;
    }

    // process the data
    if (magic == 0x414c454d) { 
        // magic number that identifies the stream as a uncompressed stream
        dst = calloc(uncompressedSize,1);
        memcpy(dst, src+4, uncompressedSize);
    } else if (magic == 0x75465a4c) { 
        // magic number that identifies the stream as a compressed stream
        dst = calloc(comp_Prebuf.size + uncompressedSize,1);
        memcpy(dst, comp_Prebuf.data, comp_Prebuf.size);
        out = comp_Prebuf.size;
        int flagCount = 0;
        int flags = 0;
        while (out < (comp_Prebuf.size+uncompressedSize)) {
            // each flag byte flags 8 literals/references, 1 per bit
            flags = (flagCount++ % 8 == 0) ? src[in++] : flags >> 1;
            if ((flags & 1) == 1) { // each flag bit is 1 for reference, 0 for literal
                int offset = src[in++];
                int length = src[in++];
                offset = (offset << 4) | (length >> 4); // the offset relative to block start
                length = (length & 0xF) + 2; // the number of bytes to copy
                // the decompression buffer is supposed to wrap around back
                // to the beginning when the end is reached. we save the
                // need for such a buffer by pointing straight into the data
                // buffer, and simulating this behaviour by modifying the
                // pointers appropriately.
                offset = (out / 4096) * 4096 + offset; 
                if (offset >= out) // take from previous block
	                offset -= 4096;
                // note: can't use System.arraycopy, because the referenced
                // bytes can cross through the current out position.
                int end = offset + length;
                while (offset < end)
	                dst[out++] = dst[offset++];
            } else { // literal
                dst[out++] = src[in++];
            }
        }
        // copy it back without the prebuffered data
        src = dst;
        dst = calloc(uncompressedSize,1);
        memcpy(dst, src + comp_Prebuf.size, uncompressedSize);
        free(src);
        *size = uncompressedSize;
        return dst;
    } else { // unknown magic number
        printf("Unknown compression type (magic number %x)\n", magic );
        return NULL;
    }
}

#include "utility.c"
#include "vcal.c"
#include "vcard.c"
#include "vtask.c"



