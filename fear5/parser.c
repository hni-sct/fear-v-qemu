#include <assert.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/tree.h>
#include "fear5/faultinjection.h"
#include "fear5/parser.h"
#include <gio/gio.h>

GFile *gfile;
GInputStream *sbase;
GDataInputStream *sdata;

static int evalxpath(const char* xpath, xmlXPathContextPtr xpath_ctx, xmlXPathObjectPtr *xpath_obj, xmlNodeSetPtr *nodes)
{
	// Evaluate the expression
	*xpath_obj = xmlXPathEvalExpression(BAD_CAST xpath, xpath_ctx);
	assert(xpath_obj);

	// Set the return value
	*nodes = (*xpath_obj)->nodesetval;
	return (*xpath_obj)->nodesetval->nodeNr;
}

int testsetup_load(const char *filename)
{
	int i;
	xmlDocPtr doc;
	xmlXPathContextPtr xpath_ctx;
	xmlXPathObjectPtr xpath_obj;
	xmlNodeSetPtr nodes = NULL;

    if (setup == NULL) {
        setup = g_new0(TestSetup, 1);
    }

	/* Open XML list of monitors */
	doc = xmlParseFile(filename);
	assert(doc);
	xpath_ctx = xmlXPathNewContext(doc);
	assert(xpath_ctx);

	/* Init monitors hash table */
	setup->monitors = g_hash_table_new(g_direct_hash, g_direct_equal);
	setup->stimulators = g_hash_table_new(g_direct_hash, g_direct_equal);

	// 1) Parse Monitors...
	evalxpath("/TestSetup/Monitors/Monitor", xpath_ctx, &xpath_obj, &nodes);
	for (i = 0; i < nodes->nodeNr; i++) {
		xmlNodePtr xml_monitor = nodes->nodeTab[i];
		const char* name_str = (const char*) xmlGetNoNsProp(xml_monitor, BAD_CAST "name");
		const char* address_str = (const char*) xmlGetNoNsProp(xml_monitor, BAD_CAST "address");

		// Store this monitor:
		MemMonitor *m = g_new0(MemMonitor, 1);
		m->name = name_str;
		sscanf(address_str, "%" PRIx64, &m->address);
		g_hash_table_insert(setup->monitors, GINT_TO_POINTER(m->address), m);
	}

	// 2) Parse Stimulators...
	evalxpath("/TestSetup/Stimulators/Stimulator", xpath_ctx, &xpath_obj, &nodes);
	for (i = 0; i < nodes->nodeNr; i++) {
		xmlNodePtr xml_monitor = nodes->nodeTab[i];
		const char* name_str = (const char*) xmlGetNoNsProp(xml_monitor, BAD_CAST "name");
		const char* address_str = (const char*) xmlGetNoNsProp(xml_monitor, BAD_CAST "address");
		const char* file_str = (const char*) xmlGetNoNsProp(xml_monitor, BAD_CAST "file");

		// Store this monitor:
		MemStimulator *s = g_new0(MemStimulator, 1);
		s->name = name_str;
		sscanf(address_str, "%" PRIx64, &s->address);
		s->file = fopen(file_str, "rb");
		if (s->file == NULL) {
			printf("ERROR: Stimulus file '%s' does not exist!\n", file_str);
			exit(1);
		}
		g_hash_table_insert(setup->stimulators, GINT_TO_POINTER(s->address), s);
	}

	// 3) Timeout
	evalxpath("/TestSetup/Timeout", xpath_ctx, &xpath_obj, &nodes);
	if (nodes->nodeNr == 1) {
		xmlNodePtr xml_timeout = nodes->nodeTab[0];
		const char* factor_str = (const char*) xmlGetNoNsProp(xml_timeout, BAD_CAST "factor");
		const char* extra_str = (const char*) xmlGetNoNsProp(xml_timeout, BAD_CAST "extra");

		// Store the timeout settings:
		sscanf(factor_str, "%f", &setup->timeout_factor);
		sscanf(extra_str, "%" PRIu64, &setup->timeout_us_extra);
	}

	/* Close XML list of monitors */
	xmlXPathFreeObject(xpath_obj);
	xmlXPathFreeContext(xpath_ctx);
	xmlFreeDoc(doc);

	return 0;
}

int mutantlist_load(const char *filename)
{
    if (setup == NULL) {
        setup = g_new0(TestSetup, 1);
    }

	gfile = g_file_new_for_path(filename);
	sbase = (GInputStream *) g_file_read(gfile, NULL, NULL);
	sdata = g_data_input_stream_new(g_buffered_input_stream_new(sbase));
	assert(sdata);

	// Count number of mutants...
	int c = 0;
	char *line = g_data_input_stream_read_line(sdata, NULL, NULL, NULL);
	while (line) {
		if (line[0] != '#') {
			c++;
		}
		g_free(line);
		line = g_data_input_stream_read_line(sdata, NULL, NULL, NULL);
	}

	// Set read pointer back to 0
	g_seekable_seek((GSeekable *) sdata, 0, G_SEEK_SET, NULL, NULL);

	setup->m_count = c;
	setup->m_index = -1;

	return 0;
}

int fear5_gotonext_mutant(void) {

	setup->m_index++;

	// Get the next mutant line from CSV file....
	char *line = g_data_input_stream_read_line(sdata, NULL, NULL, NULL);
	while (line && line[0] == '#') {
		g_free(line);
		line = g_data_input_stream_read_line(sdata, NULL, NULL, NULL);
	}

	if (!line) {
		return 1;
		// Note: This should protect the parser from reading outside
		//       of the mutant list file's memory...
	}

	// Parse it...
	gchar **tok = g_strsplit(line, ",", -1);

	sscanf(tok[0], "%d", &setup->current.id);
	sscanf(tok[1], "%d", &setup->current.kind);
	sscanf(tok[2], "%" PRIu64, &setup->current.addr_reg_mem);
	sscanf(tok[3], "%" PRIu64, &setup->current.nr_access);
	sscanf(tok[4], "%" PRIx64, &setup->current.biterror);

	g_strfreev(tok);
	g_free(line);

	return 0;
}

void mutantlist_close(void) {
	if (sdata) g_object_unref(sdata);
	if (sbase) g_object_unref(sbase);
	if (gfile) g_object_unref(gfile);
}