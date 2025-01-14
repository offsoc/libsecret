#!/usr/bin/env python

import unittest

from gi.repository import MockService as Mock
from gi.repository import Secret, GLib

Mock.start("mock-service-normal.py")

STORE_SCHEMA = Secret.Schema.new("org.mock.type.Store",
	Secret.SchemaFlags.NONE,
	{
		"number": Secret.SchemaAttributeType.INTEGER,
		"string": Secret.SchemaAttributeType.STRING,
		"even": Secret.SchemaAttributeType.BOOLEAN,
	}
)

class TestRemove(unittest.TestCase):
	def setUp(self):
		Mock.start("mock-service-normal.py")

	def tearDown(self):
		Mock.stop()

	def testSynchronous(self):
		attributes = { "number": "1", "string": "one", "even": "false" }

		password = Secret.password_lookup_sync(STORE_SCHEMA, attributes, None)
		self.assertEqual("111", password)

		deleted = Secret.password_remove_sync(STORE_SCHEMA, attributes, None)
		self.assertEqual(True, deleted)

	def testSyncNotFound(self):
		attributes = { "number": "11", "string": "one", "even": "true" }

		password = Secret.password_lookup_sync(STORE_SCHEMA, attributes, None)
		self.assertEqual(None, password)

		deleted = Secret.password_remove_sync(STORE_SCHEMA, attributes, None)
		self.assertEqual(False, deleted)

	def testAsynchronous(self):
		loop = GLib.MainLoop(None, False)

		def on_result_ready(source, result, unused):
			loop.quit()
			deleted = Secret.password_remove_finish(result)
			self.assertEquals(True, deleted)

		Secret.password_remove(STORE_SCHEMA, { "number": "2", "string": "two" },
		                       None, on_result_ready, None)

		loop.run()

	def testAsyncNotFound(self):
		loop = GLib.MainLoop(None, False)

		def on_result_ready(source, result, unused):
			loop.quit()
			deleted = Secret.password_remove_finish(result)
			self.assertEquals(False, deleted)

		Secret.password_remove(STORE_SCHEMA, { "number": "7", "string": "five" },
		                       None, on_result_ready, None)

		loop.run()

if __name__ == '__main__':
		unittest.main()
