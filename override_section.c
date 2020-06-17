#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <elf.h>
#include <libelf.h>
#include <memory.h>
#include <err.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "defs.h"

#define RUN_THIS_SECTION_SIZE (100000)

static size_t shdrstrndx;

static Elf_Scn *get_section_by_name(Elf *elf_ref, const char *scn_name, size_t shdrstrndx);

static int pack_file_in_section(Elf* elf_ref, char *elf_map, char *section_name, char *file_to_pack_path) {
    Elf_Scn *dest_section;
    Elf64_Shdr *section_header;
    struct stat s;
    int file_to_pack_fd = -1;
    char *file_to_pack_map = NULL;
    void *start_of_section = NULL;
    int status = EXIT_SUCCESS;

    dest_section = get_section_by_name(elf_ref, section_name, shdrstrndx);
    section_header = elf64_getshdr(dest_section);

    file_to_pack_fd = open(file_to_pack_path, O_RDONLY, NULL);
    if (file_to_pack_fd == -1) {
        fprintf(stderr, "Failed opening %s ", file_to_pack_path);
        perror("");
        CLEAN(EXIT_FAILURE);
    }

    fstat(file_to_pack_fd, &s);
    size_t file_to_pack_size = (size_t) s.st_size;

    if (file_to_pack_size > RUN_THIS_SECTION_SIZE) {
        printf("The given injected ELF is bigger than %d\n", RUN_THIS_SECTION_SIZE);
        CLEAN(EXIT_FAILURE);
    }

    file_to_pack_map = mmap(NULL, file_to_pack_size, PROT_READ, MAP_PRIVATE, file_to_pack_fd, 0);
    if (file_to_pack_map == MAP_FAILED) {
        perror("Failed mmaping");
        CLEAN(EXIT_FAILURE);
    }

    start_of_section = elf_map + section_header->sh_offset;
    size_t *tmp = start_of_section;
    *tmp = file_to_pack_size;

    memcpy(start_of_section + sizeof(size_t), file_to_pack_map, file_to_pack_size);

    if (msync(elf_map, file_to_pack_size + sizeof(size_t), MS_SYNC) == -1) {
        perror("Failed msync");
        CLEAN(EXIT_FAILURE);
    }

cleanup:
    if (file_to_pack_fd > 0) close(file_to_pack_fd);
    if (NULL != file_to_pack_map) munmap(file_to_pack_map, file_to_pack_size);

    return status;
}

static Elf_Scn *get_section_by_name(Elf *elf_ref, const char *scn_name, size_t shdrstrndx) {
    Elf_Scn *scn;     // Section index struct
    Elf64_Shdr *shdr;     // Section struct
    char *section_name;

    scn = NULL;
    // Loop over all sections in the ELF object
    while ((scn = elf_nextscn(elf_ref, scn)) != NULL) {
        // Given a Elf Scn pointer, retrieve the associated section header
        if ((shdr = elf64_getshdr(scn)) != shdr)
            errx(EXIT_FAILURE, "getshdr() failed: %s.", elf_errmsg(-1));

        // Retrieve the name of the section
        if ((section_name = elf_strptr(elf_ref, shdrstrndx, shdr->sh_name)) == NULL)
            errx(EXIT_FAILURE, "elf_strptr() failed: %s.", elf_errmsg(-1));

        // If the section is the one we want... (in my case, it is one of the main file sections)
        if (!strcmp(section_name, scn_name)) return scn;

    }
    return NULL;
}

int main(int argc, char **argv) {
    int status;
    int injecting_fd = -1;
    Elf *elf_ref = NULL;
    char *injecting;
    struct stat s;
    char *injecting_file_data = NULL;

    if (argc != 4) {
        printf("\n Usage: %s <path_to_injecting_elf> <path_to_injected> <path_to_so>\n", argv[0]);
        CLEAN(EXIT_FAILURE);
    }

    injecting = argv[1];
    injecting_fd = open(injecting, O_RDWR, NULL);
    if (injecting_fd == -1) {
        perror("Failed opening injecting");
        CLEAN(EXIT_FAILURE);
    }

    /* Protect from using a lower ELF version and initialize ELF library */
    if (elf_version(EV_CURRENT) == EV_NONE) {
        printf("ELF library init failed: %s\n", elf_errmsg(-1));
        CLEAN(EXIT_FAILURE);
    }

    elf_ref = elf_begin(injecting_fd, ELF_C_RDWR, NULL);

    if (elf_kind(elf_ref) != ELF_K_ELF) {
        printf("Program is not an ELF binary\n");
        CLEAN(EXIT_FAILURE);
    }

    elf_getshdrstrndx(elf_ref, &shdrstrndx);

    fstat(injecting_fd, &s);
    size_t injecting_size = (size_t) s.st_size;
    injecting_file_data = mmap(NULL, injecting_size, PROT_READ | PROT_WRITE, MAP_SHARED, injecting_fd, 0);
    if (injecting_file_data == MAP_FAILED) {
        perror("Failed mmaping");
        CLEAN(EXIT_FAILURE);
    }

    // Pack the original ELF
    CHECK(pack_file_in_section(elf_ref, injecting_file_data, ".runthissection", argv[2]));
    // Now pack .so tool
    CHECK(pack_file_in_section(elf_ref, injecting_file_data, ".sosection", argv[3]));

cleanup:
    if (NULL != injecting_file_data) munmap(injecting_file_data, injecting_size);
    elf_end(elf_ref);
    if (injecting_fd > 0) close(injecting_fd);

    return status;
}