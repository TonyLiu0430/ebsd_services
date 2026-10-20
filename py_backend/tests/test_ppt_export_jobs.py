import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

import app.services.reports as reports_service


class PptExportJobTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        self.original_job_dir = reports_service.PPT_JOB_DIR
        reports_service.PPT_JOB_DIR = Path(self.tempdir.name)
        with reports_service.PPT_JOBS_LOCK:
            reports_service.PPT_JOBS.clear()

    def tearDown(self) -> None:
        with reports_service.PPT_JOBS_LOCK:
            reports_service.PPT_JOBS.clear()
        reports_service.PPT_JOB_DIR = self.original_job_dir
        self.tempdir.cleanup()

    def test_completed_job_writes_a_downloadable_pptx_then_removes_it(self) -> None:
        job = reports_service.PptJob(
            id="report-job",
            user_id="user-1",
            created_at=time.time(),
            updated_at=time.time(),
        )
        with reports_service.PPT_JOBS_LOCK:
            reports_service.PPT_JOBS[job.id] = job

        with patch("app.services.reports.SessionLocal", return_value=Mock()), patch(
            "app.services.reports.build_ppt_report_context", return_value={"selected_sample": "Cu-U"}
        ), patch("app.services.reports.build_pptx_report", return_value=b"pptx-content"):
            reports_service.run_ppt_report_job(job.id, reports_service.ReportPptRequest(sample="Cu-U"), {"id": "user-1"})

        self.assertEqual(job.status, "completed")
        self.assertEqual(job.file_path.read_bytes(), b"pptx-content")
        reports_service.remove_ppt_job(job.id)
        self.assertFalse(job.file_path.exists())
        self.assertNotIn(job.id, reports_service.PPT_JOBS)

    def test_completed_job_payload_uses_public_api_download_url(self) -> None:
        job = reports_service.PptJob(
            id="report-job",
            user_id="user-1",
            created_at=time.time(),
            updated_at=time.time(),
            status="completed",
        )

        payload = reports_service.ppt_job_payload(job)

        self.assertEqual(
            payload["download_url"],
            "/api/reports/pptx/report-job/download",
        )

    def test_expired_completed_job_and_file_are_cleaned_up(self) -> None:
        path = reports_service.PPT_JOB_DIR / "expired.pptx"
        path.write_bytes(b"old-pptx")
        job = reports_service.PptJob(
            id="expired-job",
            user_id="user-1",
            created_at=0,
            updated_at=time.time() - reports_service.PPT_JOB_TTL_SECONDS - 1,
            status="completed",
            file_path=path,
        )
        with reports_service.PPT_JOBS_LOCK:
            reports_service.PPT_JOBS[job.id] = job

        reports_service.cleanup_ppt_jobs()

        self.assertFalse(path.exists())
        self.assertNotIn(job.id, reports_service.PPT_JOBS)


if __name__ == "__main__":
    unittest.main()
