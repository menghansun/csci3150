#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/stat.h>

#pragma pack(push,1)
struct BootEntry {
  uint8_t BS_jmpBoot[3];
  uint8_t BS_OEMName[8];
  uint16_t BPB_BytsPerSec;
  uint8_t BPB_SecPerClus;
  uint16_t BPB_RsvdSecCnt;
  uint8_t BPB_NumFATs;
  uint16_t BPB_RootEntCnt;
  uint16_t BPB_TotSec16;
  uint8_t BPB_Media;
  uint16_t BPB_FATSz16;
  uint16_t BPB_SecPerTrk;
  uint16_t BPB_NumHeads;
  uint32_t BPB_HiddSec;
  uint32_t BPB_TotSec32;
  uint32_t BPB_FATSz32;
  uint16_t BPB_ExtFlags;
  uint16_t BPB_FSVer;
  uint32_t BPB_RootClus;
  uint16_t BPB_FSInfo;
  uint16_t BPB_BkBootSec;
  uint8_t BPB_Reserved[12];
  uint8_t BS_DrvNum;
  uint8_t BS_Reserved1;
  uint8_t BS_BootSig;
  uint32_t BS_VolID;
  uint8_t BS_VolLab[11];
  uint8_t BS_FilSysType[8];
};
struct DirEntry {
  uint8_t DIR_Name[11];
  uint8_t DIR_Attr;
  uint8_t DIR_NTRes;
  uint8_t DIR_CrtTimeTenth;
  uint16_t DIR_CrtTime;
  uint16_t DIR_CrtDate;
  uint16_t DIR_LstAccDate;
  uint16_t DIR_FstClusHI;
  uint16_t DIR_WrtTime;
  uint16_t DIR_WrtDate;
  uint16_t DIR_FstClusLO;
  uint32_t DIR_FileSize;
};
#pragma pack(pop)

struct BootEntry *boot_entry;
struct DirEntry *dir_entry;
int firstFAT, dataArea, clusterSize;

void printusage(char *dir){
  printf("Usage: %s -d [device filename] [other arguments]\n", dir);
  printf("-i                   Print file system information\n");
  printf("-l                   List the root directory\n");
  printf("-r target -o dest    Recover the target deleted file\n");
  printf("-x target            Cleanse the target deleted file\n");
}

void getdataArea(){
  firstFAT = boot_entry->BPB_BytsPerSec * boot_entry->BPB_RsvdSecCnt;
  dataArea = boot_entry->BPB_FATSz32 * boot_entry->BPB_NumFATs * boot_entry->BPB_BytsPerSec + firstFAT;
  clusterSize = boot_entry->BPB_BytsPerSec * boot_entry->BPB_SecPerClus;
}

void printinfo(char *diskpath){
  FILE *fp = fopen(diskpath, "r");
  boot_entry = malloc(sizeof(struct BootEntry));
	fread(boot_entry, sizeof(struct BootEntry), 1, fp);
  getdataArea();
  fclose(fp);
  printf("Number of FATs = %u\n", boot_entry->BPB_NumFATs);
  printf("Number of bytes per sector = %u\n", boot_entry->BPB_BytsPerSec);
  printf("Number of sectors per cluster = %u\n", boot_entry->BPB_SecPerClus);
  printf("Number of reserved sectors = %u\n", boot_entry->BPB_RsvdSecCnt);
  printf("First FAT starts at byte = %u\n", firstFAT);
  printf("Data area starts at byte = %u\n", dataArea);
}

void getname(char *name){
  int a,i;
  for(i=0;i<8;i++){
    if (dir_entry->DIR_Name[i] == ' ') break;
		name[i] = dir_entry->DIR_Name[i];
    a = i;
  }
	if (dir_entry->DIR_Name[8] != ' ') {
    a++;
    name[a] = '.';
    for(i=8;i<11;i++){
      if (dir_entry->DIR_Name[i] == ' ') break;
      a++;
      name[a] = dir_entry->DIR_Name[i];
    }
  }
  if (dir_entry->DIR_Attr == 0x10){
    a++;
    name[a] = '/';
  }
  name[a+1] = '\0';
}

void listDir(char *diskpath){
  FILE *fp = fopen(diskpath, "r+");
  boot_entry = malloc(sizeof(struct BootEntry));
	fread(boot_entry, sizeof(struct BootEntry), 1, fp);
  getdataArea();
  int i, start, count =0;
  int *fat = malloc(boot_entry->BPB_NumFATs * boot_entry->BPB_BytsPerSec * boot_entry->BPB_FATSz32);
	pread(fileno(fp), fat, boot_entry->BPB_NumFATs * boot_entry->BPB_BytsPerSec * boot_entry->BPB_FATSz32,(unsigned int)(boot_entry->BPB_RsvdSecCnt * boot_entry->BPB_BytsPerSec));
	unsigned int rootSpan = 0;
  int *ff = malloc(1009 * 2);
	for (i = boot_entry->BPB_RootClus; i < 0x0FFFFFF7; i = fat[i]){
    ff[rootSpan] = i;
    rootSpan++;

  }
  dir_entry = malloc(sizeof(struct DirEntry));
  for(i = 0; i < rootSpan; i++){
    start = dataArea + (ff[i]-2) * clusterSize;
    int startc = start + clusterSize;
    fseek(fp, start, 0);
    fread(dir_entry, sizeof(struct DirEntry), 1, fp);
    while(dir_entry->DIR_Name[0] != 0 && start < startc){
        while(dir_entry->DIR_Attr == 0x0f){
          count++;
          start = start + 32;
          fseek(fp, start, 0);
          printf("%d, LFN entry\n", count);
          fread(dir_entry, sizeof(struct DirEntry), 1, fp);
        }
        if(start >= startc){
          break;
        }
        unsigned char name[13];
        int startClustor = (dir_entry->DIR_FstClusHI<<16) + dir_entry->DIR_FstClusLO;
        getname(name);
        if(name[0] == 0xE5){
          name[0] = 63;
        }
        count++;
        printf("%d, %s, %d, %d\n", count, name, dir_entry->DIR_FileSize, startClustor);
      start = start + 32;
      fseek(fp, start, 0);
      fread(dir_entry, sizeof(struct DirEntry), 1, fp);
    }
  }
  fclose(fp);
}

void recoverFile(char *diskpath, char *target,char *outputtarget){
  FILE *fp = fopen(diskpath, "r+");
  boot_entry = malloc(sizeof(struct BootEntry));
	fread(boot_entry, sizeof(struct BootEntry), 1, fp);
  getdataArea();
  int i, start, find =0;
  int *fat = malloc(boot_entry->BPB_NumFATs * boot_entry->BPB_BytsPerSec * boot_entry->BPB_FATSz32);
	pread(fileno(fp), fat, boot_entry->BPB_NumFATs * boot_entry->BPB_BytsPerSec * boot_entry->BPB_FATSz32,(unsigned int)(boot_entry->BPB_RsvdSecCnt * boot_entry->BPB_BytsPerSec));
	unsigned int rootSpan = 0;
  int *ff = malloc(1009 * 2);
	for (i = boot_entry->BPB_RootClus; i < 0x0FFFFFF7; i = fat[i]){
    ff[rootSpan] = i;
    rootSpan++;
  }
  unsigned char tmpname[13];
  tmpname[0] = 0xE5;
  for(i = 1; i < 13; i++){
    tmpname[i] = target[i];
  }
  dir_entry = malloc(sizeof(struct DirEntry));
  for(i = 0; i < rootSpan; i++){
    start = dataArea + (ff[i]-2) * clusterSize;
    int startc = start + clusterSize;
    fseek(fp, start, 0);
    fread(dir_entry, sizeof(struct DirEntry), 1, fp);
    while(dir_entry->DIR_Name[0] != 0 && start < startc){
        unsigned char name[13];
        int i;
        int startClustor = (dir_entry->DIR_FstClusHI<<16) + dir_entry->DIR_FstClusLO;
        getname(name);
        if(!strcmp(name, tmpname)){
          find = 1;
          if (fat[startClustor] == 0 ) {
            char *ReadData = malloc(clusterSize);
            FILE *fPtr = fopen(outputtarget, "w+");
            if(!fPtr){
              printf("%s: failed to open\n", outputtarget);
              break;
            }
            pread(fileno(fp), ReadData, clusterSize, dataArea + (startClustor - 2) * clusterSize);
            fwrite (ReadData, dir_entry->DIR_FileSize, 1, fPtr);
            fclose(fPtr);
            chmod(outputtarget, 0755);
            printf("%s: recovered\n", target);
            break;
          }
          else if((dir_entry->DIR_FileSize == 0)&&(startClustor == 0)){
            FILE *fPtr = fopen(outputtarget, "w+");
            if(!fPtr){
              printf("%s: failed to open\n", outputtarget);
              break;
            }
            chmod(outputtarget, 0755);
            printf("%s: recovered\n", target);
            break;
          }
          else{
            printf("%s: error - fail to recover\n", target);
            break;
          }
        }
      start = start + 32;
      fseek(fp, start, 0);
      fread(dir_entry, sizeof(struct DirEntry), 1, fp);
    }
  }
  if(!find){
    printf("%s: error - file not found\n", target);
  }
  fclose(fp);
}

void cleanFile(char *diskpath, char *target){
  FILE *fp = fopen(diskpath, "r+");
  boot_entry = malloc(sizeof(struct BootEntry));
	fread(boot_entry, sizeof(struct BootEntry), 1, fp);
  getdataArea();
  int i, start, find =0;
  int *fat = malloc(boot_entry->BPB_NumFATs * boot_entry->BPB_BytsPerSec * boot_entry->BPB_FATSz32);
	pread(fileno(fp), fat, boot_entry->BPB_NumFATs * boot_entry->BPB_BytsPerSec * boot_entry->BPB_FATSz32,(unsigned int)(boot_entry->BPB_RsvdSecCnt * boot_entry->BPB_BytsPerSec));
	unsigned int rootSpan = 0;
  int *ff = malloc(1009 * 2);
	for (i = boot_entry->BPB_RootClus; i < 0x0FFFFFF7; i = fat[i]){
    ff[rootSpan] = i;
    rootSpan++;
  }
  unsigned char tmpname[13];
  tmpname[0] = 0xE5;
  for(i = 1; i < 13; i++){
    tmpname[i] = target[i];
  }
  dir_entry = malloc(sizeof(struct DirEntry));
  for(i = 0; i < rootSpan; i++){
    start = dataArea + (ff[i]-2) * clusterSize;
    int startc = start + clusterSize;
    fseek(fp, start, 0);
    fread(dir_entry, sizeof(struct DirEntry), 1, fp);
    while(dir_entry->DIR_Name[0] != 0 && start < startc){
        unsigned char name[13];
        int startClustor = (dir_entry->DIR_FstClusHI<<16) + dir_entry->DIR_FstClusLO;
        getname(name);
        if(!strcmp(name, tmpname)){
          find = 1;
          if (fat[startClustor] == 0) {
            char zero[dir_entry->DIR_FileSize];
            for(i = 0; i < dir_entry->DIR_FileSize; i++){
              zero[i] = 0;
            }
            pwrite(fileno(fp), zero, dir_entry->DIR_FileSize, dataArea + (startClustor - 2) * clusterSize);
            printf("%s: cleansed\n", target);
            break;
          }
          else{
            printf("%s: error - fail to cleanse\n", target);
            break;
          }

        }
      start = start + 32;
      fseek(fp, start, 0);
      fread(dir_entry, sizeof(struct DirEntry), 1, fp);
    }
  }
  if(!find){
    printf("%s: error - file not found\n", target);
  }
  fclose(fp);
}

int main(int argc, char **argv){
  if(argc == 4){
    if(!strcmp(argv[1],"-d")){
      if(!strcmp(argv[3],"-i")){
        printinfo(argv[2]);
        return 0;
      }
      else if(!strcmp(argv[3],"-l")){
        listDir(argv[2]);
        return 0;
      }
    }
    printusage(argv[0]);
    return 0;
  }
  else if(argc == 5){
    if(strcmp(argv[1],"-d") || strcmp(argv[3], "-x")){
      printusage(argv[0]);
      return 0;
    }
    cleanFile(argv[2], argv[4]);
    return 0;
  }
  else if (argc == 7){
    if(strcmp(argv[1],"-d") || strcmp(argv[3],"-r") || strcmp(argv[5],"-o")){
      printusage(argv[0]);
      return 0;
    }
    recoverFile(argv[2], argv[4], argv[6]);
    return 0;
  }
  else{
    printusage(argv[0]);
  }
  return 0;
}
