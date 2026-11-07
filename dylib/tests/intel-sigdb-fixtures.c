// Intel resolver guards against a synthetic Mach-O image; no Steam installation needed.
#include "../resolver/resolver.h"
#include "../resolver/sigdb.h"
#include "../util/log.h"

#include <mach/machine.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uuid/uuid.h>

#define FIXTURE_UUID "8f14cfc1-9e29-4d3e-a833-6933a52217fe"
#define PATTERN "55 48 89 E5 53 48 83 EC 20 C3"

static _Alignas(4096) uint8_t image[0x3000];
static const uint8_t function[] = {0x55, 0x48, 0x89, 0xe5, 0x53, 0x48, 0x83, 0xec, 0x20, 0xc3};
static struct uuid_command *identity;
static struct segment_command_64 *text_segment;
static int checks, failures;

static void check(int ok, const char *label) {
    checks++;
    if (!ok) { fprintf(stderr, "FAIL: %s\n", label); failures++; }
}

static void reset_image(void) {
    memset(image, 0, sizeof(image));
    struct mach_header_64 *mh = (void *)image;
    mh->magic = MH_MAGIC_64;
    mh->cputype = CPU_TYPE_X86_64;
    mh->ncmds = 4;
    text_segment = (void *)(mh + 1);
    text_segment->cmd = LC_SEGMENT_64;
    text_segment->cmdsize = sizeof(*text_segment) + sizeof(struct section_64);
    strcpy(text_segment->segname, "__TEXT");
    text_segment->vmsize = text_segment->filesize = 0x2000;
    text_segment->nsects = 1;
    struct section_64 *section = (void *)(text_segment + 1);
    strcpy(section->segname, "__TEXT");
    strcpy(section->sectname, "__text");
    section->addr = 0x400;
    section->size = 0x1400;

    struct segment_command_64 *linkedit = (void *)(section + 1);
    linkedit->cmd = LC_SEGMENT_64;
    linkedit->cmdsize = sizeof(*linkedit);
    strcpy(linkedit->segname, "__LINKEDIT");
    linkedit->vmaddr = linkedit->fileoff = 0x2000;
    linkedit->vmsize = linkedit->filesize = 0x1000;
    struct linkedit_data_command *starts = (void *)(linkedit + 1);
    starts->cmd = LC_FUNCTION_STARTS;
    starts->cmdsize = sizeof(*starts);
    starts->dataoff = 0x2200;
    starts->datasize = 5;
    memcpy(image + starts->dataoff, "\x81\x08\xff\x03\x00", 5);
    identity = (void *)(starts + 1);
    identity->cmd = LC_UUID;
    identity->cmdsize = sizeof(*identity);
    uuid_parse(FIXTURE_UUID, identity->uuid);
    mh->sizeofcmds = (uint32_t)((uint8_t *)(identity + 1) - (uint8_t *)(mh + 1));
    memset(image + 0x400, 0x90, 0x1400);
    memcpy(image + 0x401, function, sizeof(function));
}

static np_sigdb_t load_json(const char *json) {
    char path[] = "/tmp/bridgeplay-intel-sigdb.XXXXXX";
    int fd = mkstemp(path);
    np_sigdb_t db = {0};
    if (fd < 0) { perror("mkstemp"); exit(2); }
    size_t length = strlen(json);
    if (write(fd, json, length) != (ssize_t)length || close(fd) != 0) {
        unlink(path);
        perror("write fixture");
        exit(2);
    }
    int rc = np_load_profile(path, &db);
    unlink(path);
    check(rc == 0, "generated fixture parses");
    return db;
}

static np_sigdb_t profile(const char *arch, const char *uuid, const char *pattern,
                        const char *extra) {
    char json[8192];
    snprintf(json, sizeof(json),
             "{\"schema_version\":3,\"profile_version\":1,\"arch\":\"%s\","
             "\"module_uuids\":{\"steamclient.dylib\":\"%s\"},"
             "\"signatures\":[{\"name\":\"test\",\"aob_hex\":\"%s\","
             "\"anchor\":{\"kind\":\"aob\"}%s}]}", arch, uuid, pattern, extra);
    return load_json(json);
}

static void resolve_case(np_sigdb_t *db, uintptr_t offset, const char *label) {
    np_resolve_result_t result = {0};
    int count = np_resolve_signatures((const void *)image, (intptr_t)image, db,
                                      "steamclient.dylib", &result);
    uintptr_t address = np_lookup_address(&result, "test");
    check(count == (offset ? 1 : 0) && address == (offset ? (uintptr_t)image + offset : 0), label);
    np_free_resolution(&result);
    np_free_profile(db);
}

static void resolver_cases(void) {
    reset_image();
    np_sigdb_t db = profile("x86_64", FIXTURE_UUID, PATTERN, "");
    resolve_case(&db, 0x401, "unique odd-address Intel instruction resolves");

    db = profile("arm64", FIXTURE_UUID, PATTERN, "");
    resolve_case(&db, 0, "ARM profile cannot enable Intel hooks");
    db = profile("", FIXTURE_UUID, PATTERN, "");
    resolve_case(&db, 0, "missing architecture fails closed");
    db = profile("x86_64", "", PATTERN, "");
    resolve_case(&db, 0, "missing UUID fails closed");
    db = profile("x86_64", "invalid", PATTERN, "");
    resolve_case(&db, 0, "malformed UUID fails closed");
    db = profile("x86_64", "9f14cfc1-9e29-4d3e-a833-6933a52217fe", PATTERN, "");
    resolve_case(&db, 0, "changed Steam UUID fails closed despite identical code");
    db = profile("x86_64", "8F14CFC1-9E29-4D3E-A833-6933A52217FE", PATTERN, "");
    resolve_case(&db, 0x401, "equivalent uppercase UUID is accepted");

    memcpy(image + 0x501, function, sizeof(function));
    db = profile("x86_64", FIXTURE_UUID, PATTERN, "");
    resolve_case(&db, 0, "duplicate byte patterns are rejected");
    reset_image();
    db = profile("x86_64", FIXTURE_UUID, "55 48 89 E5 GG", "");
    resolve_case(&db, 0, "nonhexadecimal byte pattern is rejected");
    db = profile("x86_64", FIXTURE_UUID, "5", "");
    resolve_case(&db, 0, "truncated byte token is rejected");
    db = profile("x86_64", FIXTURE_UUID, "", "");
    resolve_case(&db, 0, "empty byte pattern cannot install a hook");
    db = profile("x86_64", FIXTURE_UUID, "?? ?? ??", "");
    resolve_case(&db, 0, "wildcard-only pattern cannot identify a hook");
    db = profile("x86_64", FIXTURE_UUID, "55 48 ?? E5 53 48 83 EC 20 C3", "");
    resolve_case(&db, 0x401, "a unique masked pattern still resolves");
    db = profile("x86_64", FIXTURE_UUID, PATTERN, ",\"deprecated\":true");
    check(np_required_for_module(&db, "steamclient.dylib") == 0, "deprecated entry is not required");
    resolve_case(&db, 0, "deprecated entry installs nothing");
    db = profile("x86_64", FIXTURE_UUID, PATTERN, ",\"match_offset\":\"bad\"");
    resolve_case(&db, 0, "nonnumeric match offset drops its pattern");
    db = profile("x86_64", FIXTURE_UUID, PATTERN, ",\"match_offset\":4096");
    resolve_case(&db, 0, "match offset cannot escape the text segment");
    db = profile("x86_64", FIXTURE_UUID, PATTERN, ",\"match_offset\":-3");
    resolve_case(&db, 0x404, "negative offset can identify an Intel midhook");

    ((struct mach_header_64 *)image)->cputype = CPU_TYPE_ARM64;
    db = profile("x86_64", FIXTURE_UUID, PATTERN, "");
    resolve_case(&db, 0, "ARM image cannot use an Intel profile");
    reset_image();
    identity->cmd = LC_SOURCE_VERSION;
    db = profile("x86_64", FIXTURE_UUID, PATTERN, "");
    resolve_case(&db, 0, "image without UUID is rejected");
    reset_image();
    identity->cmdsize = 0;
    db = profile("x86_64", FIXTURE_UUID, PATTERN, "");
    resolve_case(&db, 0, "zero-sized UUID command is rejected without looping");
    reset_image();
    identity->cmdsize = sizeof(struct load_command);
    db = profile("x86_64", FIXTURE_UUID, PATTERN, "");
    resolve_case(&db, 0, "truncated UUID command is rejected");
    reset_image();
    strcpy(text_segment->segname, "__OTHER");
    db = profile("x86_64", FIXTURE_UUID, PATTERN, "");
    resolve_case(&db, 0, "image without text segment is rejected");
}

static void parser_cases(const char *directory) {
    char path[1024], line[1024], name[256];
    snprintf(path, sizeof(path), "%s/EXPECTED", directory);
    FILE *expected = fopen(path, "r");
    if (!expected) { perror(path); exit(2); }
    int status, count = 0;
    while (fgets(line, sizeof(line), expected)) {
        if (sscanf(line, "%d %255s", &status, name) != 2) continue;
        snprintf(path, sizeof(path), "%s/%s", directory, name);
        if (access(path, R_OK) != 0)
            snprintf(path, sizeof(path), "%s/live/%s", directory, name);
        np_sigdb_t db = {0};
        int loaded = np_load_profile(path, &db) == 0;
        check(loaded == (status != 2), name);
        np_free_profile(&db);
        count++;
    }
    fclose(expected);
    check(count >= 39, "all shared parser fixtures were exercised");
    printf("Intel parser coverage: %d shared fixtures (ARM live anchors are not exercised)\n", count);
}

int main(int argc, char **argv) {
    np_log_level = -1;
    parser_cases(argc > 1 ? argv[1] : "dylib/tests/sigdb-fixtures");
    resolver_cases();
    printf("intel-sigdb-fixtures: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
