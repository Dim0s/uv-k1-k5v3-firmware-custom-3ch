#include "app/chFrScanner.h"
#include "app/dtmf.h"
#include "audio.h"
#include "functions.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

void COMMON_KeypadLockToggle() 
{

    if (gScreenToDisplay != DISPLAY_MENU &&
        gCurrentFunction != FUNCTION_TRANSMIT)
    {   // toggle the keyboad lock

        #ifdef ENABLE_VOICE
            gAnotherVoiceID = gEeprom.KEY_LOCK ? VOICE_ID_UNLOCK : VOICE_ID_LOCK;
        #endif

        gEeprom.KEY_LOCK = !gEeprom.KEY_LOCK;

        gRequestSaveSettings = true;
    }
}

void COMMON_SetHighlightVfo(uint8_t vfo)
{
    if (vfo >= NUM_VFOS)
        vfo = 0;
    if (gHighlightVfo == vfo)
        return;

    /* Highlight moved: drop live DTMF so the old row restores mod/power/SQL. */
    gDTMF_RX_live_timeout = 0;
    DTMF_clear_input_box_memory();
    gHighlightVfo = vfo;
}

void COMMON_SelectPttVfo(uint8_t vfo)
{
    if (vfo >= NUM_VFOS)
        vfo = 0;
    gEeprom.TX_VFO = vfo;
    gEeprom.RX_VFO = vfo;
    COMMON_SetHighlightVfo(vfo);
    /* Only retarget VFO pointers here. Do NOT call RADIO_SetupRegisters():
     * that forces RX config and can race with RADIO_PrepareTX(). */
    RADIO_SelectVfos();
}

void COMMON_SyncOpVfoToHighlight(void)
{
    /* Keep the operating VFO aligned with the home-screen invert row.
     * Do not touch RX_VFO (may be mid-receive) or call RADIO_SetupRegisters(). */
    if (gHighlightVfo >= NUM_VFOS)
        return;
    if (gEeprom.TX_VFO == gHighlightVfo)
        return;

    gEeprom.TX_VFO = gHighlightVfo;
    RADIO_SelectVfos();
}

void COMMON_SwitchVFOs()
{
#ifdef ENABLE_SCAN_RANGES    
    gScanRangeStart = 0;
#endif
    gEeprom.TX_VFO = (uint8_t)((gEeprom.TX_VFO + 1u) % NUM_VFOS);
    gEeprom.RX_VFO = gEeprom.TX_VFO;
    COMMON_SetHighlightVfo(gEeprom.TX_VFO);

    if (gInputBoxIndex > 0) {
        gInputBoxIndex = 0;
        gHasVfoBackup = false;
    }

    gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_OFF;
    gEeprom.DUAL_WATCH       = DUAL_WATCH_CHAN_A;

    gRequestSaveSettings  = 1;
    gFlagReconfigureVfos  = true;
    gScheduleDualWatch = true;

    gRequestDisplayScreen = DISPLAY_MAIN;
}

void COMMON_SwitchVFOMode()
{
#ifdef ENABLE_NOAA
    if (gEeprom.VFO_OPEN && !IS_NOAA_CHANNEL(gTxVfo->CHANNEL_SAVE))
#else
    if (gEeprom.VFO_OPEN)
#endif
    {
        if (gInputBoxIndex > 0) {
            gInputBoxIndex = 0;
            gHasVfoBackup = false;
        }

        if (IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE))
        {   // swap to frequency mode
            gEeprom.ScreenChannel[gEeprom.TX_VFO] = gEeprom.FreqChannel[gEeprom.TX_VFO];
            #ifdef ENABLE_VOICE
                gAnotherVoiceID        = VOICE_ID_FREQUENCY_MODE;
            #endif
            gRequestSaveVFO            = true;
            gVfoConfigureMode          = VFO_CONFIGURE_RELOAD;
            return;
        }

        uint16_t Channel = RADIO_FindNextChannel(gEeprom.MrChannel[gEeprom.TX_VFO], 1, false, 0);
        if (Channel != 0xFFFF)
        {   // swap to channel mode
            gEeprom.ScreenChannel[gEeprom.TX_VFO] = Channel;
            #ifdef ENABLE_VOICE
                AUDIO_SetVoiceID(0, VOICE_ID_CHANNEL_MODE);
                AUDIO_SetDigitVoice(1, Channel + 1);
                gAnotherVoiceID = (VOICE_ID_t)0xFE;
            #endif
            gRequestSaveVFO     = true;
            gVfoConfigureMode   = VFO_CONFIGURE_RELOAD;
            return;
        }
    }
}