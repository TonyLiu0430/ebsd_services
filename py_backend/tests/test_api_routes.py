import unittest
from unittest.mock import AsyncMock, patch

from app.api import ebsd as ebsd_routes


class EbsdRouteForwardingTests(unittest.IsolatedAsyncioTestCase):
    async def test_pair_upload_forwards_ebsd_file_to_service(self) -> None:
        request = object()
        crc = object()
        cpr = object()
        ebsd_file = object()
        db = object()

        with patch.object(
            ebsd_routes.ebsd_service,
            "save_ebsd_pair",
            new=AsyncMock(return_value={"id": "pair-id"}),
        ) as save_pair:
            result = await ebsd_routes.save_ebsd_pair(
                request,
                crc,
                cpr,
                ebsd_file,
                "sample",
                "A-1",
                "name",
                "folder",
                "v1",
                "sample-1",
                1,
                db,
            )

        self.assertEqual(result, {"id": "pair-id"})
        save_pair.assert_awaited_once_with(
            request,
            crc,
            cpr,
            ebsd_file,
            "sample",
            "A-1",
            "name",
            "folder",
            "v1",
            "sample-1",
            1,
            db,
        )
