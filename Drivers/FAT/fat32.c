#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct
{
    uint8_t  BootJumpIns[3];        // jmp short + nop
    uint8_t  OemIdentifier[8];      // "MSWIN4.1"

    uint16_t BytesPerSector;        // Typically 512
    uint8_t  SectorsPerCluster;     // Cluster size
    uint16_t ReservedSectors;       // FAT32 usually 32
    uint8_t  FatCount;              // Number of FATs
    uint16_t DirEntryCount;         // 0 for FAT32
    uint16_t TotalSectors16;        // 0 for FAT32
    uint8_t  MediaDescriptor;       // 0xF8 for HDD
    uint16_t SectorsPerTrack;
    uint16_t Heads;
    uint32_t HiddenSectors;
    uint32_t TotalSectors32;        // Large sector count

    // FAT32 Extended BIOS Parameter Block
    uint32_t SectorsPerFAT32;
    uint16_t ExtFlags;
    uint16_t FSVersion;
    uint32_t RootCluster;
    uint16_t FSInfoSector;
    uint16_t BackupBootSector;
    uint8_t  Reserved0[12];         // Must be zero

    // Extended Boot Record
    uint8_t  DriveNumber;           // 0x80
    uint8_t  Reserved1;             // Reserved (usually 0)
    uint8_t  BootSignature;         // 0x29 if VolumeID/Label present
    uint32_t VolumeID;              // Serial number
    uint8_t  VolumeLabel[11];       // Padded ASCII
    uint8_t  SystemID[8];           // "FAT32   "

} __attribute__((packed)) BootSector;

typedef struct {
    uint8_t  Name[11];              // 8.3 filename format
    uint8_t  Attributes;            // File attributes
    uint8_t  Reserved;              // Reserved for Windows NT
    uint8_t  CreatedTimeTenths;     // Tenths of a second
    uint16_t CreatedTime;           // Time file was created
    uint16_t CreatedDate;           // Date file was created
    uint16_t AccessedDate;          // Last access date
    uint16_t FirstClusterHigh;      // High word of first cluster
    uint16_t ModifiedTime;          // Time of last write
    uint16_t ModifiedDate;          // Date of last write
    uint16_t FirstClusterLow;       // Low word of first cluster
    uint32_t Size;                  // File size in bytes
} __attribute__((packed)) DirectoryEntry;

typedef struct {
    uint32_t LeadSignature;            // 0x41615252 — FSInfo signature 1
    uint8_t  Reserved1[480];           // Usually zero
    uint32_t StructSignature;          // 0x61417272 — FSInfo signature 2
    uint32_t FreeClusterCount;         // Number of free clusters (or 0xFFFFFFFF if unknown)
    uint32_t NextFreeCluster;          // Cluster number to start searching from
    uint8_t  Reserved2[12];            // Usually zero
    uint32_t TrailSignature;           // 0xAA550000 — FSInfo signature 3
} __attribute__((packed)) FSInfo;

BootSector BootSectorGV;
uint32_t* fatGV = NULL;

DirectoryEntry* RootDirGV = NULL;
uint32_t RootDirEndGV;

FSInfo FSinfoSectorGV;

bool ReadBootSector(FILE* disk) {
    fseek(disk, 0, SEEK_SET);
    return fread(&BootSectorGV, sizeof(BootSectorGV), 1, disk) > 0;
}

bool ReadSectors(FILE* disk, uint32_t lba, uint32_t count, void* bufferOut) {
    int seek_result = fseek(disk, lba * BootSectorGV.BytesPerSector, SEEK_SET);
    if (seek_result != 0) {
        printf("ERROR: fseek failed at LBA %u\n", lba);
        return false;
    }
    bool out = fread(bufferOut, BootSectorGV.BytesPerSector, count, disk) == count;
    printf("OUTPUT (READ_SECTORS): %d\n", out);
    printf("COUNT: %d\n", count);
    return out;
}

bool ReadFsInfo(FILE* disk) {
    return ReadSectors(disk, BootSectorGV.FSInfoSector, 1, &FSinfoSectorGV);
}

bool ReadFat(FILE* disk) {
    fatGV = (uint32_t*) malloc(BootSectorGV.SectorsPerFAT32 * BootSectorGV.BytesPerSector);
    return ReadSectors(disk, BootSectorGV.ReservedSectors, BootSectorGV.SectorsPerFAT32, fatGV);
}

uint32_t ClusterToLBA(uint32_t cluster) {
    uint32_t firstDataSector = BootSectorGV.ReservedSectors + (BootSectorGV.FatCount * BootSectorGV.SectorsPerFAT32);
    return firstDataSector + (cluster - 2) * BootSectorGV.SectorsPerCluster;
}

bool ReadRootDir(FILE* disk) {
    uint32_t cluster = BootSectorGV.RootCluster;
    uint32_t clusterSize = BootSectorGV.SectorsPerCluster * BootSectorGV.BytesPerSector;
    RootDirGV = NULL;
    RootDirEndGV = ClusterToLBA(cluster);  // store start LBA here

    DirectoryEntry* temp = NULL;
    size_t totalSize = 0;
    while (cluster < 0x0FFFFFF8) {
        temp = (DirectoryEntry*) realloc(RootDirGV, totalSize + clusterSize);
        if (!temp) return false;
        RootDirGV = temp;

        uint32_t lba = ClusterToLBA(cluster);

        printf("LBA: %d\n", lba);
        printf("Cluster: %d\n", cluster);

        if (!ReadSectors(disk, lba, BootSectorGV.SectorsPerCluster, (uint8_t*)RootDirGV + totalSize)) {
            return false;
        }

        totalSize += clusterSize;
        cluster = fatGV[cluster] & 0x0FFFFFFF; // next cluster in FAT
    }

    BootSectorGV.DirEntryCount = totalSize / sizeof(DirectoryEntry);  // recalc count
    return true;
}

DirectoryEntry* FindFile(const char* name) {
    for (uint32_t i = 0; i < BootSectorGV.DirEntryCount; i++) {
        if (memcmp(name, RootDirGV[i].Name, 11) == 0) {
            return &RootDirGV[i];
        }
    }
    return NULL;
}

bool ReadFile(DirectoryEntry* fEntry, FILE* disk, uint8_t* outBuffer) {
    uint32_t currentCluster = fEntry->FirstClusterLow |
                              (fEntry->FirstClusterHigh << 16);

    size_t bytesPerCluster = BootSectorGV.SectorsPerCluster * BootSectorGV.BytesPerSector;
    size_t totalRead = 0;
    bool success = true;

    while (currentCluster < 0x0FFFFFF8) {
        uint32_t lba = ClusterToLBA(currentCluster);
        if (!ReadSectors(disk, lba, BootSectorGV.SectorsPerCluster, outBuffer + totalRead)) {
            success = false;
            break;
        }

        totalRead += bytesPerCluster;
        currentCluster = fatGV[currentCluster] & 0x0FFFFFFF;
    }

    return success;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Syntax: [%s] <disk image> <file name>\n", argv[0]);
        return -1;
    }

    FILE* disk = fopen(argv[1], "rb");
    if (!disk) {
        fprintf(stderr, "Cannot open Disk Image, ERROR!");
        return -11;
    }

    if(!ReadBootSector(disk)) {
        fprintf(stderr, "Could not read boot sector, ERROR!");
        return -12;
    }

    if (!ReadFsInfo(disk)) {
        fprintf(stderr, "Could not read FAT, ERROR!");
        free(fatGV);
        return -13;
    }

    if (!ReadRootDir(disk)) {
        fprintf(stderr, "Could not read Root DIR, ERROR!");
        free(RootDirGV);
        free(fatGV);
        return -14;
    }

    DirectoryEntry* fileEntry = FindFile(argv[2]);
    if (!fileEntry) {
        fprintf(stderr, "Could not find File, ERROR!");
        free(RootDirGV);
        free(fatGV);
        return -15;
    }

    uint8_t* buffer = (uint8_t*) malloc(fileEntry->Size + BootSectorGV.BytesPerSector);
    if (!ReadFile(fileEntry, disk, buffer)) {
        fprintf(stderr, "Could not read File, ERROR!");
        free(RootDirGV);
        free(fatGV);
        free(buffer);
        return -16;
    }

    for (size_t i = 0; i < fileEntry->Size; i++) {
        if (isprint(buffer[i])){
            fputc(buffer[i], stdout);
        } else {
            printf("<%02x>", buffer[i]);
        }
    }
    printf("\n");

    free(buffer);
    free(RootDirGV);
    free(fatGV);
    return 0;
}