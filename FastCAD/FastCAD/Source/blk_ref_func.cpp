#include "blk_ref_func.h"
#include "../../AcadFuncs/Source/acad_funcs_header.h"
#include "../../AcadFuncs/Source/Wrap/acad_obj_wrap.h"

#include <iostream>
#include <windows.h>

void BlkRefFunc::CopyBlkName()
{
	try
	{
		AcDbObjectIdArray ids = ARXFuncs::GetObjIdsByPicking();
		if (0 == ids.length())
			return;

		AcDbObjectId picked_id = ids[0];
		ObjectWrap<AcDbBlockReference> blk_ref(DBObject::OpenObjectById<AcDbBlockReference>(picked_id));
		if (nullptr == blk_ref.object)
			return;

		AcDbObjectId blk_id = blk_ref.object->blockTableRecord();
		ObjectWrap<AcDbBlockTableRecord> source_blk(DBObject::OpenObjectById<AcDbBlockTableRecord>(blk_id));
		if (nullptr == source_blk.object)
			return;

		AcString blk_name = AcString();
		source_blk.object->getName(blk_name);
		//std::string test = std::string("abcadsfa");
		int len = (blk_name.length() + 1) * sizeof(wchar_t);
		HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
		memcpy((LPWSTR)GlobalLock(hMem), blk_name.constPtr(), len);
		GlobalUnlock(hMem);
		OpenClipboard(0);
		EmptyClipboard();
		SetClipboardData(CF_UNICODETEXT, hMem);
		CloseClipboard();
	}
	catch (...)
	{

	}
}

std::list<AcString> replace_blk_names = std::list<AcString>();
static AcDbObjectIdArray GetAllContainedBlocked(AcDbObjectId blk_id);
void BlkRefFunc::CopyBlk()
{
	replace_blk_names.clear();
	try
	{
		AcDbObjectId blk_id = AcDbObjectId::kNull;
		{
			AcDbObjectIdArray ids = ARXFuncs::GetObjIdsByPicking();
			if (0 == ids.length())
				return;

			AcDbObjectId picked_id = ids[0];
			ObjectWrap<AcDbBlockReference> blk_ref(DBObject::OpenObjectById<AcDbBlockReference>(picked_id));
			if (nullptr == blk_ref.object)
				return;

			blk_id = blk_ref.object->blockTableRecord();
			ObjectWrap<AcDbBlockTableRecord> source_blk(DBObject::OpenObjectById<AcDbBlockTableRecord>(blk_id));
			if (nullptr == source_blk.object)
				return;
		}

		AcDbObjectIdArray ids = GetAllContainedBlocked(blk_id);
		for (int i = 0; i < ids.length(); i++)
		{
			ObjectWrap<AcDbBlockTableRecord> blk_tbl_rcd(DBObject::OpenObjectById<AcDbBlockTableRecord>(ids[i]));
			if (nullptr == blk_tbl_rcd.object)
				throw int(1);

			AcString name = AcString();
			blk_tbl_rcd.object->getName(name);
			replace_blk_names.push_back(name);
		}

		ads_name ads;
		acedSSAdd(NULL, NULL, ads);
		ads_name tmp = { 0, 0 };
		Acad::ErrorStatus ret = acdbGetAdsName(tmp, ids[0]);
		acedSSAdd(tmp, ads, ads);

		acedCommandS(RTSTR, L"COPYCLIP", RTPICKS, ads, RTSTR, L"", 0);
	}
	catch (...)
	{

	}
}

static AcDbObjectId BlkNameExisted(AcString blk_name)
{
	try
	{
		ObjectWrap<AcDbBlockTable> blk_tbl(DBObject::GetBlockTable(acdbHostApplicationServices()->workingDatabase()));
		AcDbObjectId blk_id;
		if (Acad::eOk != blk_tbl.object->getAt(blk_name.constPtr(), blk_id))
			return AcDbObjectId::kNull;

		ObjectWrap<AcDbBlockTableRecord> blk_tbl_rcd(DBObject::OpenObjectById<AcDbBlockTableRecord>(blk_id));
		if (nullptr == blk_tbl_rcd.object)
			return AcDbObjectId::kNull;

		return blk_id;
	}
	catch (...)
	{

	}

	return AcDbObjectId::kNull;
}

static AcDbObjectIdArray ContainedBlocks(AcDbObjectId blk_id);
static AcDbObjectIdArray GetAllContainedBlocked(AcDbObjectId blk_id)
{
	AcDbObjectIdArray ids = AcDbObjectIdArray();
	ids.append(blk_id);

	AcDbObjectIdArray tmp_ids = AcDbObjectIdArray();
	tmp_ids.append(blk_id);

	while (tmp_ids.length() > 0)
	{
		AcDbObjectId tmp_id = tmp_ids[0];
		tmp_ids.removeAt(0);

		AcDbObjectIdArray contained_ids = ContainedBlocks(tmp_id);
		tmp_ids.append(contained_ids);
		ids.append(contained_ids);
	}

	return ids;
}

static AcDbObjectIdArray ContainedBlocks(AcDbObjectId blk_id)
{
	AcDbObjectIdArray ids = AcDbObjectIdArray();

	ObjectWrap<AcDbBlockTableRecord> blk_tbl_rcd(DBObject::OpenObjectById<AcDbBlockTableRecord>(blk_id));
	if (nullptr == blk_tbl_rcd.object)
		return ids;

	AcDbBlockTableRecordIterator* blk_iter = nullptr;
	blk_tbl_rcd.object->newIterator(blk_iter);
	for (; !blk_iter->done(); blk_iter->step())
	{
		ObjectWrap<AcDbEntity> sub_ent(nullptr);
		blk_iter->getEntity(sub_ent.object);

		if (sub_ent.object->isKindOf(AcDbBlockReference::desc()))
		{
			ids.append(AcDbBlockReference::cast(sub_ent.object)->blockTableRecord());
		}
	}

	return ids;
}

struct KeepRefBlk
{
	AcDbObjectId blk_id;
	AcDbObjectIdArray ref_ids;

	KeepRefBlk() :
		blk_id(AcDbObjectId::kNull),
		ref_ids(AcDbObjectIdArray())
	{}

	KeepRefBlk(AcDbObjectId bid, AcDbObjectIdArray rids) :
		blk_id(bid),
		ref_ids(rids)
	{}
};

void BlkRefFunc::PasteBlk()
{
	try
	{
		std::list<KeepRefBlk> ref_blks = std::list<KeepRefBlk>();
		for (auto iter = replace_blk_names.begin(); iter != replace_blk_names.end(); iter++)
		{
			AcDbObjectId blk_id = BlkNameExisted(*iter);
			AcDbObjectIdArray ids;
			if (AcDbObjectId::kNull != blk_id)
			{
				ObjectWrap<AcDbBlockTableRecord> blk_tbl_rcd(DBObject::OpenObjectById<AcDbBlockTableRecord>(blk_id));
				AcString rep_name = AcString(*iter);
				rep_name.append(L"_1");
				blk_tbl_rcd.object->upgradeOpen();
				blk_tbl_rcd.object->setName(rep_name.constPtr());
				blk_tbl_rcd.object->getBlockReferenceIds(ids);
			}

			ref_blks.push_back(KeepRefBlk(blk_id, ids));
		}

		acedCommandS(RTSTR, L"PASTECLIP", RTSTR, L"", 0);

		for (auto iter = ref_blks.begin(); iter != ref_blks.end(); iter++)
		{
			AcDbObjectId rep_id = BlkNameExisted(replace_blk_names.front());
			for (int i = 0; i < iter->ref_ids.length(); i++)
			{
				ObjectWrap<AcDbBlockReference> blk_ref(DBObject::OpenObjectById<AcDbBlockReference>(iter->ref_ids[i]));
				blk_ref.object->upgradeOpen();
				blk_ref.object->setBlockTableRecord(rep_id);
			}

			if (AcDbObjectId::kNull != iter->blk_id)
			{
				ObjectWrap<AcDbBlockTableRecord> blk_tbl_rcd(DBObject::OpenObjectById<AcDbBlockTableRecord>(iter->blk_id));
				blk_tbl_rcd.object->upgradeOpen();
				blk_tbl_rcd.object->erase();
			}
		}
	}
	catch(...)
	{ }
}