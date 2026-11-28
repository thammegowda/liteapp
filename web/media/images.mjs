// Image attachments for vision models, which read PNG and JPEG. Other formats (WebP, GIF, ...) are re-encoded, and
// images wider or taller than `maxSide` pixels or larger than `maxBytes` are downscaled and recompressed.
export async function prepareImage(file, {maxSide = 2048, maxBytes = 2 * 1024 * 1024} = {}) {
    const bitmap = await createImageBitmap(file);
    try {
        const scale = Math.min(1, maxSide / Math.max(bitmap.width, bitmap.height));
        const native = file.type === 'image/png' || file.type === 'image/jpeg';
        if (native && scale === 1 && file.size <= maxBytes) return file;
        const canvas = new OffscreenCanvas(Math.round(bitmap.width * scale), Math.round(bitmap.height * scale));
        canvas.getContext('2d').drawImage(bitmap, 0, 0, canvas.width, canvas.height);
        const png = file.type === 'image/png';
        return canvas.convertToBlob(png ? {type: 'image/png'} : {type: 'image/jpeg', quality: 0.9});
    } finally {
        bitmap.close();
    }
}
