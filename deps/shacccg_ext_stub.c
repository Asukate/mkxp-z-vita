/* VitaShaRK calls these optional shader-compiler extension toggles.
 * The proven Vita build linked no-op implementations because the installed
 * VitaSDK does not provide them. Keep that dependency seam in source. */
void sceShaccCgExtEnableExtensions(void) {}
void sceShaccCgExtDisableExtensions(void) {}
