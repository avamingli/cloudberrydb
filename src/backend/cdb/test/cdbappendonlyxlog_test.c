#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include "cmockery.h"

#include "postgres.h"
#include "utils/memutils.h"

#include "../cdbappendonlyxlog.c"

#include "access/transam.h"
#include "catalog/pg_tablespace.h"

static int
check_relfilenode_function(const LargestIntegralType arg1, const LargestIntegralType arg2)
{
	const RelFileNode *value = (const RelFileNode *) arg1;
	const RelFileNode *check_value = (const RelFileNode *) arg2;
	return RelFileNodeEquals(*value, *check_value);
}

/*
 * Test that XLogAOSegmentFile will be called when we cannot find the AO
 * segment file.
 */
static void
ao_invalid_segment_file_test(uint8 xl_info)
{
	RelFileNode relfilenode;
	XLogRecord record;
	XLogReaderState *mockrecord;
	xl_ao_target xlaotarget;
	xl_ao_insert xlaoinsert;
	xl_ao_truncate xlaotruncate;

	/* create mock transaction log */
	relfilenode.spcNode = DEFAULTTABLESPACE_OID;
	relfilenode.dbNode = 12087 /* postgres database */;
	relfilenode.relNode = FirstNormalObjectId;

	xlaotarget.node = relfilenode;
	xlaotarget.segment_filenum = 2;
	xlaotarget.offset = 12345;

	record.xl_info = xl_info;
	record.xl_rmid = RM_APPEND_ONLY_ID;

	mockrecord = XLogReaderAllocate(DEFAULT_XLOG_SEG_SIZE, NULL, XL_ROUTINE(), NULL);

	if (xl_info == XLOG_APPENDONLY_INSERT)
	{
		xlaoinsert.target = xlaotarget;
		mockrecord->main_data = (char *) &xlaoinsert;
	}
	else if (xl_info == XLOG_APPENDONLY_TRUNCATE)
	{
		xlaotruncate.target = xlaotarget;
		mockrecord->main_data = (char *) &xlaotruncate;
	}

	/* mock to not find AO segment file */
	expect_any(PathNameOpenFile, fileName);
	expect_any(PathNameOpenFile, fileFlags);
	will_return(PathNameOpenFile, -1);

	/* XLogAOSegmentFile should be called with our mock relfilenode and segment file number */
	expect_check(XLogAOSegmentFile, &rnode, check_relfilenode_function, &relfilenode);
	expect_value(XLogAOSegmentFile, segmentFileNum, xlaotarget.segment_filenum);
	will_be_called(XLogAOSegmentFile);

	/* run test */
	if (xl_info == XLOG_APPENDONLY_INSERT)
		ao_insert_replay(mockrecord);
	else if (xl_info == XLOG_APPENDONLY_TRUNCATE)
		ao_truncate_replay(mockrecord);
}

/*
 * Test that ao_insert_replay will call XLogAOSegmentFile when we cannot find
 * the AO segment file.
 */
static void
test_ao_insert_replay_invalid_segment_file(void **state)
{
	ao_invalid_segment_file_test(XLOG_APPENDONLY_INSERT);
}

/*
 * Test that ao_truncate_replay will call XLogAOSegmentFile when we cannot find
 * the AO segment file.
 */
static void
test_ao_truncate_replay_invalid_segment_file(void **state)
{
	ao_invalid_segment_file_test(XLOG_APPENDONLY_TRUNCATE);
}

int
main(int argc, char* argv[])
{
	cmockery_parse_arguments(argc, argv);

	const UnitTest tests[] = {
		unit_test(test_ao_insert_replay_invalid_segment_file),
		unit_test(test_ao_truncate_replay_invalid_segment_file)
	};

	MemoryContextInit();

	return run_tests(tests);
}
