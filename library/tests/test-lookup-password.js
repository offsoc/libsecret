
const Mock = imports.gi.MockService;
const Secret = imports.gi.Secret;
const GLib = imports.gi.GLib;

const JsUnit = imports.jsUnit;
const assertEquals = JsUnit.assertEquals;
const assertRaises = JsUnit.assertRaises;
const assertTrue = JsUnit.assertTrue;

Mock.start("mock-service-normal.py");

const STORE_SCHEMA = new Secret.Schema.new("org.mock.type.Store",
	Secret.SchemaFlags.NONE,
	{
		"number": Secret.SchemaAttributeType.INTEGER,
		"string": Secret.SchemaAttributeType.STRING,
		"even": Secret.SchemaAttributeType.BOOLEAN,
	}
);

/* Synchronous */

var password = Secret.password_lookup_sync (STORE_SCHEMA, { "number": "1", "even": "false" }, null);
assertEquals("111", password);

var password = Secret.password_lookup_sync (STORE_SCHEMA, { "number": "5", "even": "true" }, null);
assertEquals(null, password);

/* Asynchronous */

var loop = new GLib.MainLoop.new(null, false);

Secret.password_lookup (STORE_SCHEMA, { "number": "2", "string": "two" },
                        null, function(source, result) {
	loop.quit();
	var password = Secret.password_lookup_finish(result);
	assertEquals("222", password);
});

loop.run();

Secret.password_lookup (STORE_SCHEMA, { "number": "7", "string": "five" },
        null, function(source, result) {
	loop.quit();
	var password = Secret.password_lookup_finish(result);
	assertEquals(null, password);
});

loop.run();

Mock.stop();
