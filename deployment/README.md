# Deployment examples

[简体中文](README_CN.md)

This directory groups the existing container recipes and Kubernetes examples.
The YAML files retain their existing services, images, namespaces and resources;
their in-tree plugin library stems follow the current build names. The Photon
examples use `/home/server/escort` as their working directory and mount `conf/`
there, matching the application image's runtime layout.
External business-plugin names remain unchanged. Review these environment-specific
examples before use; moving them does not establish working deployment acceptance.

Run image build commands from the repository root so `COPY .` keeps the source
checkout as its build context:

```sh
docker build -t docker/ark-alpine-build:latest -f deployment/docker/ark-alpine-build .
docker build -t docker/ark-alpine-runtime:latest -f deployment/docker/ark-alpine-runtime .
docker build -t ark:escort -f deployment/Dockerfile .
```

The application Dockerfile still uses the build image for both stages. The
separate runtime recipe is retained as an existing alternative; it is not selected
by the application recipe. These recipes retain their pre-existing dependency
and image choices and have not been validated against a container engine here.

Cluster example paths are now `deployment/ark_consul.yaml`,
`deployment/photon_config.yaml`, `deployment/photon_game_deployment.yaml` and
`deployment/photon_master_deployment.yaml`. Update external build commands that
used the root `Dockerfile`, `docker/` or `kubernetes/` paths. Runtime launch helpers
are documented in the [application guide](../docs/guides/applications.md#run-one-application).

Source samples live in `config/` and `resources/`. The application image stages
them as runtime `conf/` and `resource/`; those runtime names are unchanged. Generate
and review `resource/meta/entity_schema.json` before deploying a Kernel-based service;
Kernel reads this JSON resource and its manifest `config` remains empty.
The retained Photon images and external business plugins require their own
environment validation.

The official MySQL and Redis plugins use owning options passed to their public
`configure()` APIs; they do not read database XML presets. Keep their application
manifest `config` values empty. Loading a plugin or mounting this ConfigMap does not
configure a database connection. See [MySQL](../docs/reference/mysql.md) and
[Redis](../docs/reference/redis.md) for consumer configuration. The database example targets remain disabled in the default build; retaining their
sources and manifests does not establish runnable examples or deployment acceptance.

Built-in application presets and Photon application ConfigMap keys use
`.app.toml`; restage files and update custom launch arguments and mounts.
The ConfigMap no longer contains Freebird or `kernel.plugin.conf`; its four
former Kernel file selections are empty. XML application and plugin configuration
payloads are retired. See the [migration mapping](../docs/service-manifests.md#built-in-application-preset-migration).

Photon's external settings now use `photon.example.plugin.toml`,
`photon.game.plugin.toml` and `photon.master.plugin.toml`. Realm and Terraria
samples under `config/` likewise select `.plugin.toml`. Their fields preserve
the old values with snake_case keys; integer and boolean values use native TOML
types. External plugin library stems are unchanged. These are mechanical sample
migrations: the old external plugins have not been shown to understand TOML.
Coordinate the schema, update their parsers and validate the owning applications
before using these samples. No container, cluster or external-plugin acceptance
is established by parsing the configuration files.
