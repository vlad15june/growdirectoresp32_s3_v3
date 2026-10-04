#ifndef LED_BOOT_STAGE_ENUM_H
#define LED_BOOT_STAGE_ENUM_H

/**
 * @brief Whether the module is still booting, as the front panel shows it.
 *
 * A boot of a configured module is seconds long when everything answers and minutes long when
 * something does not, and what an installer standing in front of the box needs from the LED is the
 * difference between "still coming up" and "up". That is one pattern, not a vocabulary: the waits
 * of a boot are told apart by the log, which names each of them with the milliseconds it took, and
 * a front panel that tries to spell them out on a boot that takes four seconds only flickers.
 *
 * BOOTING ends at RUNNING: from there the LED is the working indication - the green heartbeat and
 * the red of a module that has lost the server - and this enum has nothing more to say.
 */
enum LedBootStageEnum
{
    /** Setup is running: the module is coming up and nothing of it is working yet. */
    LED_BOOT_STAGE_BOOTING = 0,
    /** Setup is over and the control loop paces the module. */
    LED_BOOT_STAGE_RUNNING = 1
};

#endif // LED_BOOT_STAGE_ENUM_H
