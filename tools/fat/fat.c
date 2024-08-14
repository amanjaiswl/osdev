#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef uint8_t bool;
#define true 1
#define false 0

typedef struct
{
    uint8_t BootJumpInstruction[3];
    uint8_t OemIdentifier[8];
    uint16_t BytesPerSector;
    uint8_t SectorsPerCluster;
    uint16_t ReservedSectors;
    uint8_t FatCount;
    uint16_t DirEntryCount;
    uint16_t TotalSectors;
    uint8_t MediaDescriptorType;
    uint16_t SectorsPerFat;
    uint16_t SectorsPerTrack;
    uint16_t Heads;
    uint32_t HiddenSectors;
    uint32_t LargeSectorCount;

    // extended boot record
    uint8_t DriveNumber;
    uint8_t _Reserved;
    uint8_t Signature;
    uint32_t VolumeId;          // serial number, value doesn't matter
    uint8_t VolumeLabel[11];    // 11 bytes, padded with spaces
    uint8_t SystemId[8];

    // ... we don't care about code ...

} __attribute__((packed)) BootSector;

typedef struct
{
    uint8_t Name[11];
    uint8_t Attributes;
    uint8_t _Reserved;
    uint8_t CreatedTimeTenths;
    uint16_t CreatedTime;
    uint16_t CreatedDate;
    uint16_t AccessedDate;
    uint16_t FirstClusterHigh;
    uint16_t ModifiedTime;
    uint16_t ModifiedDate;
    uint16_t FirstClusterLow;
    uint32_t Size;
} __attribute__((packed)) DirectoryEntry;

BootSector g_BootSector;
uint8_t* g_Fat = NULL;
DirectoryEntry* g_RootDirectory = NULL;
uint32_t g_RootDirectoryEnd;

bool readBootSector(FILE* disk)
{
    bool result = fread(&g_BootSector, sizeof(g_BootSector), 1, disk) > 0;
    if (result) {
        printf("Boot sector read successfully.\n");
        printf("Bytes per sector: %d\n", g_BootSector.BytesPerSector);
        printf("Sectors per cluster: %d\n", g_BootSector.SectorsPerCluster);
        printf("Reserved sectors: %d\n", g_BootSector.ReservedSectors);
        printf("FAT count: %d\n", g_BootSector.FatCount);
        printf("Directory entries: %d\n", g_BootSector.DirEntryCount);
        printf("Total sectors: %d\n", g_BootSector.TotalSectors);
        printf("Sectors per FAT: %d\n", g_BootSector.SectorsPerFat);
    } else {
        fprintf(stderr, "Failed to read boot sector.\n");
    }
    return result;
}

bool readSectors(FILE* disk, uint32_t lba, uint32_t count, void* bufferOut)
{
    bool ok = true;
    ok = ok && (fseek(disk, lba * g_BootSector.BytesPerSector, SEEK_SET) == 0);
    ok = ok && (fread(bufferOut, g_BootSector.BytesPerSector, count, disk) == count);
    return ok;
}

bool readFat(FILE* disk)
{
    g_Fat = (uint8_t*) malloc(g_BootSector.SectorsPerFat * g_BootSector.BytesPerSector);
    if (!g_Fat) {
        fprintf(stderr, "Failed to allocate memory for FAT.\n");
        return false;
    }
    bool result = readSectors(disk, g_BootSector.ReservedSectors, g_BootSector.SectorsPerFat, g_Fat);
    if (!result) {
        fprintf(stderr, "Failed to read FAT.\n");
    }
    return result;
}

bool readRootDirectory(FILE* disk)
{
    uint32_t lba = g_BootSector.ReservedSectors + g_BootSector.SectorsPerFat * g_BootSector.FatCount;
    uint32_t size = sizeof(DirectoryEntry) * g_BootSector.DirEntryCount;
    uint32_t sectors = (size / g_BootSector.BytesPerSector);
    if (size % g_BootSector.BytesPerSector > 0)
        sectors++;

    g_RootDirectoryEnd = lba + sectors;
    g_RootDirectory = (DirectoryEntry*) malloc(sectors * g_BootSector.BytesPerSector);
    if (!g_RootDirectory) {
        fprintf(stderr, "Failed to allocate memory for root directory.\n");
        return false;
    }
    bool result = readSectors(disk, lba, sectors, g_RootDirectory);
    if (!result) {
        fprintf(stderr, "Failed to read root directory.\n");
    }
    return result;
}

void toUpperAndPad(char* dest, const char* src)
{
    int i, j;
    for (i = 0; i < 11; i++)
        dest[i] = ' ';
    
    for (i = 0; i < 8 && src[i] != '.' && src[i] != '\0'; i++)
        dest[i] = toupper((unsigned char)src[i]);

    if (src[i] == '.') {
        i++;
        for (j = 0; j < 3 && src[i] != '\0'; j++, i++)
            dest[8 + j] = toupper((unsigned char)src[i]);
    }
}

DirectoryEntry* findFile(const char* name)
{
    char formattedName[11];
    toUpperAndPad(formattedName, name);

    printf("Looking for file: '%.11s'\n", formattedName);

    for (uint32_t i = 0; i < g_BootSector.DirEntryCount; i++)
    {
        if (g_RootDirectory[i].Name[0] == 0x00)
            break;  // End of directory

        if (g_RootDirectory[i].Name[0] == 0xE5)
            continue;  // Deleted file

        if (g_RootDirectory[i].Attributes & 0x0F)
            continue;  // Skip non-file entries

        printf("Comparing with: '%.11s'\n", g_RootDirectory[i].Name);
        if (memcmp(formattedName, g_RootDirectory[i].Name, 11) == 0) {
            printf("File found!\n");
            return &g_RootDirectory[i];
        }
    }

    printf("File not found.\n");
    return NULL;
}

bool readFile(DirectoryEntry* fileEntry, FILE* disk, uint8_t* outputBuffer)
{
    bool ok = true;
    uint16_t currentCluster = fileEntry->FirstClusterLow;

    do {
        uint32_t lba = g_RootDirectoryEnd + (currentCluster - 2) * g_BootSector.SectorsPerCluster;
        ok = ok && readSectors(disk, lba, g_BootSector.SectorsPerCluster, outputBuffer);
        outputBuffer += g_BootSector.SectorsPerCluster * g_BootSector.BytesPerSector;

        uint32_t fatIndex = currentCluster * 3 / 2;
        if (currentCluster % 2 == 0)
            currentCluster = (*(uint16_t*)(g_Fat + fatIndex)) & 0x0FFF;
        else
            currentCluster = (*(uint16_t*)(g_Fat + fatIndex)) >> 4;

    } while (ok && currentCluster < 0x0FF8);

    return ok;
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        printf("Syntax: %s <disk image> <file name>\n", argv[0]);
        return -1;
    }

    FILE* disk = fopen(argv[1], "rb");
    if (!disk) {
        fprintf(stderr, "Cannot open disk image %s!\n", argv[1]);
        return -1;
    }

    if (!readBootSector(disk)) {
        fprintf(stderr, "Could not read boot sector!\n");
        fclose(disk);
        return -2;
    }

    if (!readFat(disk)) {
        fprintf(stderr, "Could not read FAT!\n");
        free(g_Fat);
        fclose(disk);
        return -3;
    }

    if (!readRootDirectory(disk)) {
        fprintf(stderr, "Could not read root directory!\n");
        free(g_Fat);
        free(g_RootDirectory);
        fclose(disk);
        return -4;
    }

    char upperFileName[13];
    strncpy(upperFileName, argv[2], 12);
    upperFileName[12] = '\0';
    for (int i = 0; upperFileName[i]; i++)
        upperFileName[i] = toupper((unsigned char)upperFileName[i]);

    printf("Searching for file: %s\n", upperFileName);
    DirectoryEntry* fileEntry = findFile(upperFileName);
    if (!fileEntry) {
        fprintf(stderr, "Could not find file %s!\n", upperFileName);
        free(g_Fat);
        free(g_RootDirectory);
        fclose(disk);
        return -5;
    }

    uint8_t* buffer = (uint8_t*) malloc(fileEntry->Size + g_BootSector.BytesPerSector);
    if (!buffer) {
        fprintf(stderr, "Could not allocate memory for file content!\n");
        free(g_Fat);
        free(g_RootDirectory);
        fclose(disk);
        return -6;
    }

    if (!readFile(fileEntry, disk, buffer)) {
        fprintf(stderr, "Could not read file %s!\n", upperFileName);
        free(g_Fat);
        free(g_RootDirectory);
        free(buffer);
        fclose(disk);
        return -7;
    }

    printf("File contents:\n");
    for (size_t i = 0; i < fileEntry->Size; i++)
    {
        if (isprint(buffer[i])) fputc(buffer[i], stdout);
        else printf("<%02x>", buffer[i]);
    }
    printf("\n");

    free(buffer);
    free(g_Fat);
    free(g_RootDirectory);
    fclose(disk);
    return 0;
}
