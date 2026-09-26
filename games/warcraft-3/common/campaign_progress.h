#ifndef WC3_CAMPAIGN_PROGRESS_H
#define WC3_CAMPAIGN_PROGRESS_H

#include "common/shared.h"

#define WC3_CAMPAIGN_PROGRESS_FILENAME "campaign-progress.orcp"
#define WC3_CAMPAIGN_PROGRESS_EDITIONS 2 // editions; ROC and TFT slots; used in progress arrays and file validation
#define WC3_CAMPAIGN_PROGRESS_CAMPAIGNS 5 // campaigns; maximum campaign slots per edition; used in progress arrays
#define WC3_CAMPAIGN_PROGRESS_MISSIONS 128 // missions; bounded mission slots per campaign; used in progress arrays

typedef enum {
    WC3_CAMPAIGN_EDITION_ROC = 0,
    WC3_CAMPAIGN_EDITION_TFT = 1,
} wc3CampaignEdition_t;

typedef struct {
    uint32_t edition;
    uint32_t campaign;
    uint32_t mission;
} wc3CampaignProgressKey_t;

typedef struct {
    uint8_t tutorial_known[WC3_CAMPAIGN_PROGRESS_EDITIONS];
    uint8_t tutorial_cleared[WC3_CAMPAIGN_PROGRESS_EDITIONS];
    uint8_t campaign_known[WC3_CAMPAIGN_PROGRESS_EDITIONS][WC3_CAMPAIGN_PROGRESS_CAMPAIGNS];
    uint8_t campaign_available[WC3_CAMPAIGN_PROGRESS_EDITIONS][WC3_CAMPAIGN_PROGRESS_CAMPAIGNS];
    uint8_t mission_known[WC3_CAMPAIGN_PROGRESS_EDITIONS][WC3_CAMPAIGN_PROGRESS_CAMPAIGNS]
                      [WC3_CAMPAIGN_PROGRESS_MISSIONS];
    uint8_t mission_available[WC3_CAMPAIGN_PROGRESS_EDITIONS][WC3_CAMPAIGN_PROGRESS_CAMPAIGNS]
                          [WC3_CAMPAIGN_PROGRESS_MISSIONS];
} wc3CampaignProgress_t;

void wc3_campaign_progress_init(wc3CampaignProgress_t *progress);
bool wc3_campaign_progress_load(cstring_t path, wc3CampaignProgress_t *progress);
bool wc3_campaign_progress_save(cstring_t path, wc3CampaignProgress_t const *progress);
bool wc3_campaign_progress_set_tutorial(wc3CampaignProgress_t *progress, uint32_t edition, bool cleared);
bool wc3_campaign_progress_set_campaign(wc3CampaignProgress_t *progress,
                                        wc3CampaignProgressKey_t key, bool available);
bool wc3_campaign_progress_set_mission(wc3CampaignProgress_t *progress,
                                       wc3CampaignProgressKey_t key, bool available);
bool wc3_campaign_progress_has_campaign(wc3CampaignProgress_t const *progress,
                                        wc3CampaignProgressKey_t key);
bool wc3_campaign_progress_campaign_available(wc3CampaignProgress_t const *progress,
                                              wc3CampaignProgressKey_t key);
bool wc3_campaign_progress_has_mission(wc3CampaignProgress_t const *progress,
                                       wc3CampaignProgressKey_t key);
bool wc3_campaign_progress_mission_available(wc3CampaignProgress_t const *progress,
                                             wc3CampaignProgressKey_t key);
int32_t wc3_campaign_progress_campaign_index(uint32_t edition, cstring_t key);

#endif
