-- Initialization script for Q-Mini-WASM data stack PostgreSQL.
-- Runs once when the Postgres container is first created (docker-entrypoint-initdb.d).
-- The database and main user are already created via POSTGRES_DB and POSTGRES_USER;
-- this script enables extensions required for LLM/quantum and app use.

-- Enable UUID generation (e.g. for primary keys, correlation IDs).
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-- Enable pgvector for embedding/vector storage (LLM and quantum state representations).
CREATE EXTENSION IF NOT EXISTS "vector";

-- Optional: grant usage on schema public to the primary user (already owns the DB).
-- GRANT ALL ON SCHEMA public TO current_user;
