#pragma once

#include <cstddef>
#include <cstdint>

namespace tunnel {

// Copy length-delimited untrusted text as a terminated UTF-8 string. Invalid
// bytes and Unicode C0/C1 controls become '?'; valid sequences stay intact.
// Returns true on replacement, truncation, or an unusable destination/source.
// Source bytes must be retained separately when lossless capture is required.
inline bool copyUtf8Bounded(char* dst, std::size_t capacity,
                            const std::uint8_t* src, std::size_t length) noexcept {
    if(!dst || capacity==0) return true;
    dst[0]='\0';
    if(!src) return length!=0;
    std::size_t input=0, output=0;
    bool changed=false;
    while(input<length) {
        const std::uint8_t lead=src[input];
        std::size_t count=1;
        std::uint32_t codepoint=lead, minimum=0;
        bool valid=true;
        if(lead<0x80) {
            // ASCII, including controls handled below.
        } else if(lead>=0xc2 && lead<=0xdf) {
            count=2; codepoint=lead & 0x1f; minimum=0x80;
        } else if(lead>=0xe0 && lead<=0xef) {
            count=3; codepoint=lead & 0x0f; minimum=0x800;
        } else if(lead>=0xf0 && lead<=0xf4) {
            count=4; codepoint=lead & 0x07; minimum=0x10000;
        } else valid=false;

        if(valid && count>length-input) valid=false;
        if(valid && count>1) {
            for(std::size_t offset=1; offset<count; ++offset) {
                const std::uint8_t byte=src[input+offset];
                if((byte & 0xc0)!=0x80) { valid=false; break; }
                codepoint=(codepoint<<6) | (byte & 0x3f);
            }
            if(valid && (codepoint<minimum || codepoint>0x10ffff ||
                         (codepoint>=0xd800 && codepoint<=0xdfff))) valid=false;
        }

        const bool control=valid && (codepoint<0x20 ||
                                     (codepoint>=0x7f && codepoint<=0x9f));
        if(!valid || control) {
            changed=true;
            if(output==capacity-1) break;
            dst[output++]='?';
            // Consume malformed input one byte at a time so a later valid
            // ASCII byte cannot be swallowed by a broken multibyte prefix.
            input+=valid ? count : 1;
        } else {
            if(count>capacity-1-output) { changed=true; break; }
            for(std::size_t offset=0; offset<count; ++offset)
                dst[output++]=static_cast<char>(src[input+offset]);
            input+=count;
        }
    }
    dst[output]='\0';
    return changed || input<length;
}

}  // namespace tunnel
