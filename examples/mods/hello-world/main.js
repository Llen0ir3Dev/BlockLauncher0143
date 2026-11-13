// Minimal NEXUS script mod.
//
// Load it with:
//   nexus mod list --dir examples/mods
//   nexus mod test examples/mods/hello-world
// or let the runtime discover the examples/mods directory:
//   nexus-runtime --headless --mods examples/mods --seconds 5

nexus.log("info", "hello-world mod loaded (version " + nexus.version + ")");

nexus.on("world.loaded", function (data) {
    nexus.log("info", "world loaded: " + JSON.stringify(data));
});

nexus.on("example.ping", function (data) {
    nexus.log("info", "ping received: " + JSON.stringify(data));
});

var block = nexus.registerBlock({
    name: "example:hello_block",
    id: 240,
    texture: "textures/blocks/hello",
    hardness: 1.5,
    lightEmission: 0.25
});

var item = nexus.registerItem({
    name: "example:hello_item",
    id: 480,
    texture: "textures/items/hello",
    maxStack: 16
});

nexus.log("info", "registered block=" + block + " item=" + item);
