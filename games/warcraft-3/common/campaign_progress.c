#include "campaign_progress.h"

#include <errno.h>
#ifndef _WIN32
#include <strings.h>
#endif

#define WC3_CAMPAIGN_PROGRESS_MAGIC MAKEFOURCC('O', 'R', 'C', 'P')
#define WC3_CAMPAIGN_PROGRESS_VERSION 1u // format version; identifies the current ORCP layout; used during file validation

typedef struct {
    cstring_t name;
    int32_t campaign[WC3_CAMPAIGN_PROGRESS_EDITIONS];
} campaignProgressName_t;

static campaignProgressName_t const campaign_progress_names[] = {
    { "Tutorial", { 0, -1 } },
    { "Human",    { 1,  1 } },
    { "Undead",   { 2,  2 } },
    { "Orc",      { 3,  3 } },
    { "NightElf", { 4,  0 } },
};

static bool campaign_progress_key_valid(wc3CampaignProgressKey_t key) {
    return key.edition < WC3_CAMPAIGN_PROGRESS_EDITIONS &&
           key.campaign < WC3_CAMPAIGN_PROGRESS_CAMPAIGNS;
}

static bool campaign_progress_write_bytes(FILE *file, void const * data, size_t size) {
    return size == 0 || fwrite(data, 1, size, file) == size;
}

static bool campaign_progress_read_bytes(FILE *file, void *data, size_t size) {
    return size == 0 || fread(data, 1, size, file) == size;
}

static bool campaign_progress_write_u32(FILE *file, uint32_t value) {
    uint8_t const bytes[4] = {
        (uint8_t)(value & 0xffu),
        (uint8_t)((value >> 8) & 0xffu),
        (uint8_t)((value >> 16) & 0xffu),
        (uint8_t)((value >> 24) & 0xffu),
    };
    return campaign_progress_write_bytes(file, bytes, sizeof(bytes));
}

static bool campaign_progress_read_u32(FILE *file, uint32_t * value) {
    uint8_t bytes[4];

    if (!campaign_progress_read_bytes(file, bytes, sizeof(bytes))) return false;
    *value = (uint32_t)bytes[0] |
             ((uint32_t)bytes[1] << 8) |
             ((uint32_t)bytes[2] << 16) |
             ((uint32_t)bytes[3] << 24);
    return true;
}

void wc3_campaign_progress_init(wc3CampaignProgress_t *progress) {
    if (progress) memset(progress, 0, sizeof(*progress));
}

bool wc3_campaign_progress_set_tutorial(wc3CampaignProgress_t *progress,
                                        uint32_t edition, bool cleared) {
    if (!progress || edition >= WC3_CAMPAIGN_PROGRESS_EDITIONS) return false;
    progress->tutorial_known[edition] = 1;
    progress->tutorial_cleared[edition] = cleared ? 1 : 0;
    return true;
}

bool wc3_campaign_progress_set_campaign(wc3CampaignProgress_t *progress,
                                        wc3CampaignProgressKey_t key, bool available) {
    if (!progress || !campaign_progress_key_valid(key)) return false;
    progress->campaign_known[key.edition][key.campaign] = 1;
    progress->campaign_available[key.edition][key.campaign] = available ? 1 : 0;
    return true;
}

bool wc3_campaign_progress_set_mission(wc3CampaignProgress_t *progress,
                                       wc3CampaignProgressKey_t key, bool available) {
    if (!progress || !campaign_progress_key_valid(key) ||
        key.mission >= WC3_CAMPAIGN_PROGRESS_MISSIONS) {
        return false;
    }
    progress->mission_known[key.edition][key.campaign][key.mission] = 1;
    progress->mission_available[key.edition][key.campaign][key.mission] = available ? 1 : 0;
    return true;
}

bool wc3_campaign_progress_has_campaign(wc3CampaignProgress_t const *progress,
                                        wc3CampaignProgressKey_t key) {
    return progress && campaign_progress_key_valid(key) &&
           progress->campaign_known[key.edition][key.campaign];
}

bool wc3_campaign_progress_campaign_available(wc3CampaignProgress_t const *progress,
                                              wc3CampaignProgressKey_t key) {
    return progress && campaign_progress_key_valid(key) &&
           progress->campaign_available[key.edition][key.campaign];
}

bool wc3_campaign_progress_has_mission(wc3CampaignProgress_t const *progress,
                                       wc3CampaignProgressKey_t key) {
    return progress && campaign_progress_key_valid(key) &&
           key.mission < WC3_CAMPAIGN_PROGRESS_MISSIONS &&
           progress->mission_known[key.edition][key.campaign][key.mission];
}

bool wc3_campaign_progress_mission_available(wc3CampaignProgress_t const *progress,
                                             wc3CampaignProgressKey_t key) {
    return progress && campaign_progress_key_valid(key) &&
           key.mission < WC3_CAMPAIGN_PROGRESS_MISSIONS &&
           progress->mission_available[key.edition][key.campaign][key.mission];
}

int32_t wc3_campaign_progress_campaign_index(uint32_t edition, cstring_t key) {
    if (!key || !*key || edition >= WC3_CAMPAIGN_PROGRESS_EDITIONS) return -1;
    FOR_LOOP(i, sizeof(campaign_progress_names) / sizeof(campaign_progress_names[0])) {
        if (!strcasecmp(key, campaign_progress_names[i].name)) {
            return campaign_progress_names[i].campaign[edition];
        }
    }
    return -1;
}

bool wc3_campaign_progress_load(cstring_t path, wc3CampaignProgress_t *progress) {
    FILE *file;
    uint32_t magic, version, editions, campaigns, missions;
    wc3CampaignProgress_t loaded;

    if (!progress) return false;
    wc3_campaign_progress_init(progress);
    if (!path || !*path) return false;

    errno = 0;
    file = fopen(path, "rb");
    if (!file) {
        if (errno != ENOENT) {
            fprintf(stderr, "Campaign progress: cannot open '%s' for reading: %s\n",
                    path, strerror(errno));
        }
        return false;
    }

    wc3_campaign_progress_init(&loaded);
    if (!campaign_progress_read_u32(file, &magic) || magic != WC3_CAMPAIGN_PROGRESS_MAGIC ||
        !campaign_progress_read_u32(file, &version) || version != WC3_CAMPAIGN_PROGRESS_VERSION ||
        !campaign_progress_read_u32(file, &editions) || editions != WC3_CAMPAIGN_PROGRESS_EDITIONS ||
        !campaign_progress_read_u32(file, &campaigns) || campaigns != WC3_CAMPAIGN_PROGRESS_CAMPAIGNS ||
        !campaign_progress_read_u32(file, &missions) || missions != WC3_CAMPAIGN_PROGRESS_MISSIONS ||
        !campaign_progress_read_bytes(file, loaded.tutorial_known, sizeof(loaded.tutorial_known)) ||
        !campaign_progress_read_bytes(file, loaded.tutorial_cleared, sizeof(loaded.tutorial_cleared)) ||
        !campaign_progress_read_bytes(file, loaded.campaign_known, sizeof(loaded.campaign_known)) ||
        !campaign_progress_read_bytes(file, loaded.campaign_available, sizeof(loaded.campaign_available)) ||
        !campaign_progress_read_bytes(file, loaded.mission_known, sizeof(loaded.mission_known)) ||
        !campaign_progress_read_bytes(file, loaded.mission_available, sizeof(loaded.mission_available))) {
        fprintf(stderr, "Campaign progress: invalid or unsupported file '%s'\n", path);
        fclose(file);
        return false;
    }

    if (fclose(file) != 0) return false;
    *progress = loaded;
    return true;
}

bool wc3_campaign_progress_save(cstring_t path, wc3CampaignProgress_t const *progress) {
    PATHSTR tmp, backup;
    FILE *file;
    bool ok;
    bool had_backup = false;

    if (!path || !*path || !progress) return false;
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp) ||
        snprintf(backup, sizeof(backup), "%s.bak", path) >= (int)sizeof(backup)) {
        fprintf(stderr, "Campaign progress: path is too long\n");
        return false;
    }

    file = fopen(tmp, "wb");
    if (!file) {
        fprintf(stderr, "Campaign progress: cannot open '%s' for writing: %s\n",
                tmp, strerror(errno));
        return false;
    }

    ok = campaign_progress_write_u32(file, WC3_CAMPAIGN_PROGRESS_MAGIC) &&
         campaign_progress_write_u32(file, WC3_CAMPAIGN_PROGRESS_VERSION) &&
         campaign_progress_write_u32(file, WC3_CAMPAIGN_PROGRESS_EDITIONS) &&
         campaign_progress_write_u32(file, WC3_CAMPAIGN_PROGRESS_CAMPAIGNS) &&
         campaign_progress_write_u32(file, WC3_CAMPAIGN_PROGRESS_MISSIONS) &&
         campaign_progress_write_bytes(file, progress->tutorial_known,
                                       sizeof(progress->tutorial_known)) &&
         campaign_progress_write_bytes(file, progress->tutorial_cleared,
                                       sizeof(progress->tutorial_cleared)) &&
         campaign_progress_write_bytes(file, progress->campaign_known,
                                       sizeof(progress->campaign_known)) &&
         campaign_progress_write_bytes(file, progress->campaign_available,
                                       sizeof(progress->campaign_available)) &&
         campaign_progress_write_bytes(file, progress->mission_known,
                                       sizeof(progress->mission_known)) &&
         campaign_progress_write_bytes(file, progress->mission_available,
                                       sizeof(progress->mission_available));
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        fprintf(stderr, "Campaign progress: failed while writing '%s'\n", tmp);
        remove(tmp);
        return false;
    }

    remove(backup);
    errno = 0;
    if (rename(path, backup) == 0) {
        had_backup = true;
    } else if (errno != ENOENT) {
        fprintf(stderr, "Campaign progress: cannot preserve previous '%s': %s\n",
                path, strerror(errno));
        remove(tmp);
        return false;
    }

    if (rename(tmp, path) != 0) {
        int const install_errno = errno;
        if (had_backup && rename(backup, path) != 0) {
            fprintf(stderr, "Campaign progress: failed to restore previous '%s': %s\n",
                    path, strerror(errno));
        }
        fprintf(stderr, "Campaign progress: cannot install '%s': %s\n",
                path, strerror(install_errno));
        remove(tmp);
        return false;
    }

    if (had_backup) remove(backup);
    return true;
}
