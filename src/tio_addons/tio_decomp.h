#pragma once
#include "tio.h"

/* Stream decompression add-on for tio.
   Comes with out-of-the-box-support for:
   - LZ4 (custom decoder)
   - inflate (via miniz)

How to use?
   Most functions receive two streams:
   - The first stream will be initialized (analoguous to tio_sopen()),
   - using the second stream as its data source.
   Refilling the first stream will pull compressed data from the second stream
   as needed and decompress that on the fly. Uses the tio_Stream::cursor pointer
   of the first stream to keep track of partial transfers.
   Note that the first stream keeps a pointer to the second stream, which must
   stay alive and valid while the first one is in use!
   After you're done with the decompressor stream you can continue using the underlying
   stream if there are more data afterwards as long as you respect the ->cursor field.

>->->-> Theory and explanation (skip if you're in a hurry) >->->->

   Most libs have a function like this:
     size_t decompressMemToMem(void *dst, size_t dstsize, const void *src, size_t srcsize)

   This is about as easy as it gets and performance-wise, this is unbeatable.
   For LZ77-style decoders that require a window to store the last xx KB of uncompressed
   output this is perfect -- they can just use the output buffer as the window.
   The downside is that all of the compressed data must be in RAM, and the uncompressed data
   must also fit into RAM. Not ideal for memory-constrained targets, and does not support streaming.

   If the decompressor in question supports streaming, you might see something like:
     size_t decompSome(DecompCtx *ctx, void *dst, size_t dstsize, const void *src, size_t srcsize);

   ... which you can call repeatedly with smaller buffers and decompress the data piecemeal.
   Unfortunately this incurs some overhead to keep track of the decompressor state,
   especially if a user feeds in a few bytes at a time. (This would be very simple to
   implement using coroutines, but unfortunately C has no such thing natively.)

   Another downside is that the user-provided buffer can not be used as window,
   so there must be some form of decompressor context (ctx) that manages its own window.
   This in turn means that the decompressor has to copy the data twice to decompress:
   Into the internal window, and into the user-supplied output buffer.
   That smells very much like fread(), which *mandates* a copy operation even if the
   requested data are in the OSes file cache and we could in theory just get a pointer.

   The tio_Stream API permits directly using the internal decompression window as data source,
   so that no extra copy step has to take place.

   Background info:
     https://fgiesen.wordpress.com/2011/11/21/buffer-centric-io/ (Ctrl+F "cool thing")

<-<-<-< /Theory and explanation <-<-<-<

*/

#ifdef __cplusplus
extern "C" {
#endif

/* --- Stream-to-stream functions. See the note at the top for details. ---
    Initializes 'sm' and stores a pointer to 'packed' in it.
    'packed' must stay alive while 'sm' is used.
    You MUST pass an allocator.
*/

/* Decompress deflate data with zlib header and footer */
TIO_EXPORT tio_error tio_sdecomp_zlib(tio_Stream* sm, tio_Stream* packed, tio_StreamFlags flags, tio_Alloc alloc, void* allocUD);

/* Decompress LZ4-framed data. */
TIO_EXPORT tio_error tio_sdecomp_LZ4_frame(tio_Stream *sm, tio_Stream *packed, tio_StreamFlags flags, tio_Alloc alloc, void* allocUD);


/* --------------
   Raw block decompression functions, for compressed streams without framing.
   Since there is no framing, the compressed format cannot be auto-detected.
   -------------- */

/* Decompress raw deflate-encoded data. */
TIO_EXPORT tio_error tio_sdecomp_deflate(tio_Stream* sm, tio_Stream* packed, tio_StreamFlags flags, tio_Alloc alloc, void* allocUD);

/* Decompress a raw LZ4 block. Since there is no end marker,
   the length of the compressed stream has to be supplied by the user. */
TIO_EXPORT tio_error tio_sdecomp_LZ4_block(tio_Stream *sm, tio_Stream *packed, size_t packedbytes, tio_StreamFlags flags, tio_Alloc alloc, void* allocUD);


#ifdef __cplusplus
}
#endif
