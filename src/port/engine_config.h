
#ifndef __ENGINE_CONFIG_H__
#define __ENGINE_CONFIG_H__

/**
 * Port to use.
 *
 * Use 1 to when the POSIX port is selected
 *
 * Default: 1 (enable)
 */
#ifndef CFG_PORT_POSIX
#    define CFG_PORT_POSIX					1
#endif

/**
 * Port to use.
 *
 * Use 1 to when the CORAL port is selected
 *
 * Default: 0 (disabled)
 */
#ifndef CFG_PORT_CORAL
#    define CFG_PORT_CORAL					0
#endif



#define CFG_USE_REGISTRY				1
#define CFG_USE_STRSUB					1
#define CFG_USE_ENGINE_CONSOLE			1
#define CFG_USE_ENGINE_DEBUG			1
#define CFG_USE_ENGINE_ENGINE			1
#define CFG_USE_ENGINE_TOASTER			1




#endif /* __ENGINE_CONFIG_H__ */
