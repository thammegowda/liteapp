export function registerExampleServices(router, variant, capabilities = {}) {
    router.register('app.status', () => ({variant, ...capabilities}));
    router.register('app.echo', ({payload}) => ({
        variant,
        value: String(payload?.value ?? ''),
    }));
}
