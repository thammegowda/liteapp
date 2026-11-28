// Binary data in JSON messages and data URLs.

export function bytesToBase64(bytes) {
    let binary = '';
    for (let offset = 0; offset < bytes.length; offset += 0x8000)
        binary += String.fromCharCode(...bytes.subarray(offset, offset + 0x8000));
    return btoa(binary);
}

export function base64ToBytes(text) {
    return Uint8Array.from(atob(text), character => character.charCodeAt(0));
}

export async function blobToDataUrl(blob) {
    return `data:${blob.type};base64,${bytesToBase64(new Uint8Array(await blob.arrayBuffer()))}`;
}
