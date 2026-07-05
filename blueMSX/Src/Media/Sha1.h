#ifndef __SHA1_HH__
#define __SHA1_HH__

#include "MsxTypes.h"

#ifdef __cplusplus
#include <string>

class SHA1
{
public:
	SHA1();
	~SHA1();

	/** Update the hash value.
	  */
	void update(const UInt8* data, unsigned len);

	/** Get the final hash as a pre-formatted string.
	  * After this method is called, calls to update() are invalid.
	  */
	const std::string& hex_digest();

private:
	void transform(const UInt8 buffer[64]);
	void finalize();
	
	UInt32 m_state[5];
	UInt64 m_count;
	UInt8  m_buffer[64];

	std::string digest;
};

extern "C" {
#endif

/* C-callable wrapper: writes the 40-char lower-case hex digest of
** `data` (`len` bytes) plus a NUL terminator into `outHex[41]`. */
void calcSha1Hex(const UInt8* data, unsigned len, char outHex[41]);

#ifdef __cplusplus
}
#endif

#endif
