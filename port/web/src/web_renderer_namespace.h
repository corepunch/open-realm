/* The Warcraft renderer owns its MDX stream readers in native builds.
 * Namespace the private copy for a browser link that also contains common/net.c. */
#define MSG_Read WC3Renderer_MSG_Read
#define MSG_ReadByte WC3Renderer_MSG_ReadByte
#define MSG_ReadLong WC3Renderer_MSG_ReadLong
