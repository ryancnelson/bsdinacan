#include "Processes.r"
resource 'SIZE' (-1) {
    reserved, acceptSuspendResumeEvents, reserved, cannotBackground,
    needsActivateOnFGSwitch, backgroundAndForeground, dontGetFrontClicks,
    ignoreChildDiedEvents, is32BitCompatible, notHighLevelEventAware,
    onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
    reserved, reserved, reserved,
    8 * 1024 * 1024, 8 * 1024 * 1024
};
