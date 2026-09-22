# Security notes

Keep `.env`, local `.env.*` files, and `firmware/include/secrets.h` out of Git and shared ZIPs. Use the supplied example files to document required values. Provider credentials belong on the backend; use distinct admin and device tokens.

Never post API keys, Wi-Fi passwords, live tokens, or private coordinates in a public issue. Use placeholders and synthetic responses when reporting a problem. For a suspected sensitive vulnerability, avoid public details until a private reporting route with the maintainer is established.

If credentials were committed or shared, revoke or rotate them at the issuing service. Removing a file or making a repository private does not invalidate a token or remove historical copies.

The packaging script excludes known local files; it is not a guarantee that source text contains no secrets. Review staged changes and archive contents before publication. Do not expose local HTTP services on an untrusted network, and retain HTTPS certificate verification for internet-hosted devices.
