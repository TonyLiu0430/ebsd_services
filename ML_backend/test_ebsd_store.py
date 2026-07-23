import unittest

from sqlalchemy import create_engine, inspect, select
from sqlalchemy.orm import Session

from ebsd_store import (
    Base,
    EbsdFile,
    EbsdPair,
    UserEbsdPair,
    create_user_pair,
    get_or_create_ebsd_file,
    get_or_create_pair,
    migrate_user_pair_target_reference,
    upsert_user_target,
)


def create_legacy_schema(engine) -> None:
    statements = [
        """
        CREATE TABLE user_ebsd_targets (
            id VARCHAR(36) NOT NULL PRIMARY KEY,
            user_id VARCHAR(36) NOT NULL,
            name VARCHAR(255) NOT NULL,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP NOT NULL,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP NOT NULL,
            CONSTRAINT uq_user_ebsd_targets_user_name UNIQUE (user_id, name)
        )
        """,
        "CREATE TABLE ebsd_pairs (id VARCHAR(36) NOT NULL PRIMARY KEY)",
        """
        CREATE TABLE user_ebsd_pairs (
            id VARCHAR(36) NOT NULL PRIMARY KEY,
            user_id VARCHAR(36) NOT NULL,
            pair_id VARCHAR(36) NOT NULL,
            display_name VARCHAR(255) NOT NULL,
            sample VARCHAR(255) NOT NULL,
            position VARCHAR(32) NOT NULL,
            folder VARCHAR(255),
            version_key VARCHAR(64),
            version_label VARCHAR(255),
            version_num INTEGER,
            material_key VARCHAR(64) NOT NULL,
            material_display_name VARCHAR(255) NOT NULL,
            material_element VARCHAR(32),
            material_phase VARCHAR(32),
            material_confidence VARCHAR(32) NOT NULL,
            material_source VARCHAR(255) NOT NULL,
            material_metadata TEXT,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP NOT NULL,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP NOT NULL,
            CONSTRAINT uq_user_ebsd_pairs_user_pair UNIQUE (user_id, pair_id)
        )
        """,
        "CREATE INDEX ix_user_ebsd_pairs_user_sample ON user_ebsd_pairs (user_id, sample)",
        "INSERT INTO ebsd_pairs (id) VALUES ('pair-1')",
        """
        INSERT INTO user_ebsd_pairs (
            id, user_id, pair_id, display_name, sample, position,
            material_key, material_display_name, material_confidence, material_source
        ) VALUES (
            'scan-1', 'user-1', 'pair-1', 'scan', 'target-a', '1',
            'unknown', 'Unknown', 'unknown', 'unknown'
        )
        """,
    ]
    with engine.begin() as connection:
        for statement in statements:
            connection.exec_driver_sql(statement)


class EbsdStoreIdentityTests(unittest.TestCase):
    def test_hash_entities_are_reused_but_scan_records_have_own_uuids(self) -> None:
        test_engine = create_engine("sqlite:///:memory:")
        Base.metadata.create_all(test_engine)

        with Session(test_engine) as db:
            target_a, _ = upsert_user_target(db, "user-1", "target-a")
            target_b, _ = upsert_user_target(db, "user-1", "target-b")
            crc, _ = get_or_create_ebsd_file(db, b"same-crc", "a.crc", ".crc")
            cpr, _ = get_or_create_ebsd_file(db, b"same-cpr", "a.cpr", ".cpr")
            pair, _ = get_or_create_pair(db, crc, cpr)

            first, _ = create_user_pair(
                db,
                "user-1",
                target_a,
                pair,
                display_name="first",
                position="1",
                folder=None,
                version_key="v1",
                version_label="target-a-1",
                version_num=1,
            )
            second, _ = create_user_pair(
                db,
                "user-1",
                target_b,
                pair,
                display_name="second",
                position="1",
                folder=None,
                version_key="v1",
                version_label="target-b-1",
                version_num=1,
            )
            third, _ = create_user_pair(
                db,
                "user-1",
                target_a,
                pair,
                display_name="another context",
                position="2",
                folder=None,
                version_key="v2",
                version_label="target-a-2",
                version_num=2,
            )
            db.commit()

            self.assertEqual({first.pair_id, second.pair_id, third.pair_id}, {pair.id})
            self.assertEqual(len({first.id, second.id, third.id}), 3)
            self.assertEqual(first.target_id, target_a.id)
            self.assertEqual(second.target_id, target_b.id)
            self.assertEqual(db.query(EbsdFile).count(), 2)
            self.assertEqual(db.query(EbsdPair).count(), 1)
            self.assertEqual(db.query(UserEbsdPair).count(), 3)

    def test_legacy_constraint_is_migrated_to_direct_target_reference(self) -> None:
        test_engine = create_engine("sqlite:///:memory:")
        create_legacy_schema(test_engine)

        migrate_user_pair_target_reference(test_engine)

        inspector = inspect(test_engine)
        columns = {column["name"]: column for column in inspector.get_columns("user_ebsd_pairs")}
        unique_sets = {
            frozenset(constraint.get("column_names") or [])
            for constraint in inspector.get_unique_constraints("user_ebsd_pairs")
        }
        foreign_keys = inspector.get_foreign_keys("user_ebsd_pairs")

        self.assertIn("target_id", columns)
        self.assertFalse(columns["target_id"]["nullable"])
        self.assertNotIn(frozenset(("user_id", "pair_id")), unique_sets)
        self.assertTrue(any(fk.get("constrained_columns") == ["target_id"] for fk in foreign_keys))

        with Session(test_engine) as db:
            migrated = db.execute(select(UserEbsdPair)).scalar_one()
            self.assertIsNotNone(migrated.target_id)
            duplicate_context = UserEbsdPair(
                id="scan-2",
                user_id=migrated.user_id,
                target_id=migrated.target_id,
                pair_id=migrated.pair_id,
                display_name="another scan record",
                sample=migrated.sample,
                position="2",
                material_key="unknown",
                material_display_name="Unknown",
                material_confidence="unknown",
                material_source="unknown",
            )
            db.add(duplicate_context)
            db.commit()
            self.assertEqual(db.query(UserEbsdPair).count(), 2)


if __name__ == "__main__":
    unittest.main()
