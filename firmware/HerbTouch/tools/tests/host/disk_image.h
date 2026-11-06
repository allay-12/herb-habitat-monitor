#ifndef HERB_DISK_IMAGE_H
#define HERB_DISK_IMAGE_H
void TestDisk_SetImage(const char *path);
void TestDisk_AllowWrites(int allow);
extern int test_disk_fail_reads;
extern unsigned test_disk_writes,test_disk_reads;
#endif
