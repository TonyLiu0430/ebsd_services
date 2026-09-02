import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

import main


class PptExportJobTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        self.original_job_dir = main.PPT_JOB_DIR
        main.PPT_JOB_DIR = Path(self.tempdir.name)
        with main.PPT_JOBS_LOCK:
            main.PPT_JOBS.clear()

    def tearDown(self) -> None:
        with main.PPT_JOBS_LOCK:
            main.PPT_JOBS.clear()
        main.PPT_JOB_DIR = self.original_job_dir
        self.tempdir.cleanup()

    def test_completed_job_writes_a_downloadable_pptx_then_removes_it(self) -> None:
        job = main.PptJob(
            id="report-job",
            user_id="user-1",
            created_at=time.time(),
            updated_at=time.time(),
        )
        with main.PPT_JOBS_LOCK:
            main.PPT_JOBS[job.id] = job

        with patch("main.SessionLocal", return_value=Mock()), patch(
            "main.build_ppt_report_context", return_value={"selected_sample": "Cu-U"}
        ), patch("main.build_pptx_report", return_value=b"pptx-content"):
            main.run_ppt_report_job(job.id, main.ReportPptRequest(sample="Cu-U"), {"id": "user-1"})

        self.assertEqual(job.status, "completed")
        self.assertEqual(job.file_path.read_bytes(), b"pptx-content")
        main.remove_ppt_job(job.id)
        self.assertFalse(job.file_path.exists())
        self.assertNotIn(job.id, main.PPT_JOBS)

    def test_completed_job_payload_uses_public_api_download_url(self) -> None:
        job = main.PptJob(
            id="report-job",
            user_id="user-1",
            created_at=time.time(),
            updated_at=time.time(),
            status="completed",
        )

        payload = main.ppt_job_payload(job)

        self.assertEqual(
            payload["download_url"],
            "/api/reports/pptx/report-job/download",
        )

    def test_expired_completed_job_and_file_are_cleaned_up(self) -> None:
        path = main.PPT_JOB_DIR / "expired.pptx"
        path.write_bytes(b"old-pptx")
        job = main.PptJob(
            id="expired-job",
            user_id="user-1",
            created_at=0,
            updated_at=time.time() - main.PPT_JOB_TTL_SECONDS - 1,
            status="completed",
            file_path=path,
        )
        with main.PPT_JOBS_LOCK:
            main.PPT_JOBS[job.id] = job

        main.cleanup_ppt_jobs()

        self.assertFalse(path.exists())
        self.assertNotIn(job.id, main.PPT_JOBS)


if __name__ == "__main__":
    unittest.main()
