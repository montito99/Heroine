//
// Created by sean on 18/04/20.
//
#include <elf.h>
#include <libelf.h>
//#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <linux/memfd.h>
#include <syscall.h>
#include "defs.h"

#define MY_ERRX(err_msg) (errx(EXIT_FAILURE, err_msg, elf_errmsg(-1)))

static size_t shdrstrndx = 0;
static Elf_Scn *get_section_by_name(Elf *elf_ref, const char *scn_name, size_t shdrstrndx);
static int unpack_section_to_memfd(Elf *elf_ref, const char *section_name, long *memfd);

static int unpack_section_to_memfd(Elf *elf_ref, const char *section_name, long *memfd) {
    int status = EXIT_SUCCESS;
    long tmp_memfd;

    Elf_Scn *sig_scn;
    Elf64_Shdr *signature_scn_header;
    Elf64_Addr section_addr;
    size_t packed_elf_size;
    sig_scn = get_section_by_name(elf_ref, section_name, shdrstrndx);
    CHECK_NOT_NULL(sig_scn);
    signature_scn_header = elf64_getshdr(sig_scn);

    tmp_memfd = syscall(SYS_memfd_create, "", 0);
    if (tmp_memfd == -1) {
        CLEAN(EXIT_FAILURE);
    }
    *memfd = tmp_memfd;

    section_addr = signature_scn_header->sh_addr;
    packed_elf_size = *(size_t *) section_addr;

    if (write((int) *memfd, (void *) section_addr + sizeof(size_t), packed_elf_size) != packed_elf_size) {
        CLEAN(EXIT_FAILURE);
    }

    cleanup:
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


int main(int argc, char **argv, char **envp) {
    int status;

    Elf *elf_ref = NULL;
    FILE *fd;
    char memfd_name_fmt[] = "/proc/self/fd/%li";
    char packed_elf_in_mem[sizeof(memfd_name_fmt)] = {0};
    char packed_so_in_mem[sizeof(memfd_name_fmt)] = {0};
    char ld_preload_env[30] = {0};
    long memfd;
    char **modified_envp = envp;
    int n_of_envp;

    fd = fopen(argv[0], "r");
    if (fd == NULL) {
        perror("Couldn't open file");
        exit(-1);
    }

    if (elf_version(EV_CURRENT) == EV_NONE) MY_ERRX("ELF library init failed: %s\n");

    elf_ref = elf_begin(fileno(fd), ELF_C_RDWR, NULL);

    if (elf_kind(elf_ref) != ELF_K_ELF) {
        fclose(fd);
        exit(-1);
    }

    elf_getshdrstrndx(elf_ref, &shdrstrndx);

    // Unpack original ELF
    memfd = -1;
    CHECK(unpack_section_to_memfd(elf_ref, ".runthissection", &memfd));

    snprintf(packed_elf_in_mem, strlen(memfd_name_fmt), memfd_name_fmt, memfd);

    // Unpack .so tool
    memfd = -1;
    if (unpack_section_to_memfd(elf_ref, ".sosection", &memfd) == EXIT_SUCCESS) {
        snprintf(packed_so_in_mem, sizeof(memfd_name_fmt), memfd_name_fmt, memfd);
        snprintf(ld_preload_env, sizeof(ld_preload_env), "LD_PRELOAD=%s", packed_so_in_mem);

        for(n_of_envp=1; envp[n_of_envp]!=NULL; n_of_envp++);

        modified_envp = (char **)malloc(n_of_envp*sizeof(char *)+1);

        if (NULL == modified_envp) {
            modified_envp = envp;
        } else {
            memcpy(modified_envp, envp, n_of_envp * sizeof(char *));
            modified_envp[n_of_envp - 1] = ld_preload_env;
            modified_envp[n_of_envp] = NULL;
        }
    }

cleanup:
    elf_end(elf_ref);
    fclose(fd);

    if (strlen(packed_elf_in_mem) > 0) {
        execve(packed_elf_in_mem, argv, modified_envp);
    }
    // In case execve failed
    if (modified_envp != envp) free(modified_envp);
    return status;
}

