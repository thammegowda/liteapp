// Drag handles that resize panes by setting a CSS length variable. Mark each handle with
//   data-resize="--name"   the variable to set on <html> (e.g. a grid column width)
//   data-edge="left"       left | right | top | bottom: the container edge the pane grows from
//   data-min="160"         the smallest size in pixels (default 120)
//   data-container=".x"    the element whose edges are measured (default: the whole window)
export function enableSplitters(root = document) {
    const style = document.documentElement.style;
    for (const splitter of root.querySelectorAll('[data-resize]')) {
        splitter.addEventListener('pointerdown', event => {
            event.preventDefault();
            splitter.setPointerCapture(event.pointerId);
            const variable = splitter.dataset.resize;
            const edge = splitter.dataset.edge ?? 'left';
            const minimum = Number(splitter.dataset.min ?? 120);
            const container = splitter.dataset.container ? document.querySelector(splitter.dataset.container) : null;
            const move = moved => {
                const box = container?.getBoundingClientRect() ??
                    {left: 0, top: 0, right: innerWidth, bottom: innerHeight};
                const size = {left: moved.clientX - box.left, right: box.right - moved.clientX,
                    top: moved.clientY - box.top, bottom: box.bottom - moved.clientY}[edge];
                style.setProperty(variable, `${Math.max(minimum, size)}px`);
            };
            splitter.addEventListener('pointermove', move);
            const release = () => splitter.removeEventListener('pointermove', move);
            splitter.addEventListener('pointerup', release, {once: true});
        });
    }
}
