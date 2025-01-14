
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

var attributes = { "number": "9", "string": "nine", "even": "false" };

var password = Secret.password_lookup_sync (STORE_SCHEMA, attributes, null);
assertEquals(null, password);

var stored = Secret.password_store_sync (STORE_SCHEMA, attributes, Secret.COLLECTION_DEFAULT,
                                         "The number nine", "999", null);
assertEquals(true, stored);

var password = Secret.password_lookup_sync (STORE_SCHEMA, attributes, null);
assertEquals("999", password);


/* Asynchronous */ 

var attributes = { "number": "888", "string": "eight", "even": "true" };

var password = Secret.password_lookup_sync (STORE_SCHEMA, attributes, null);
assertEquals(null, password);

var loop = new GLib.MainLoop.new(null, false);

Secret.password_store (STORE_SCHEMA, attributes, null, "The number eight", "888",
                       null, function(source, result) {
	loop.quit();
	var stored = Secret.password_store_finish(result);
	assertEquals(true, stored);
});

loop.run();

var password = Secret.password_lookup_sync (STORE_SCHEMA, attributes, null);
assertEquals("888", password);

Mock.stop();
