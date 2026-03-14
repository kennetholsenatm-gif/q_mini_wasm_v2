#!/usr/bin/env node
/**
 * Build the frontend and serve it with nginx in Docker (same as production).
 * Usage: npm run preview (from wui/frontend)
 * Requires: Docker. Backend should be on host:8000 for API proxy (host.docker.internal).
 */
import { spawnSync } from "child_process";
import path from "path";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const frontendDir = path.resolve(__dirname, "..");
const distPath = path.join(frontendDir, "dist");
const configPath = path.join(frontendDir, "nginx.preview.conf");

console.log("Building frontend...");
const build = spawnSync("npm", ["run", "build"], {
  cwd: frontendDir,
  stdio: "inherit",
  shell: true,
});
if (build.status !== 0) process.exit(build.status ?? 1);

console.log("Serving with nginx on http://localhost:4173");
const docker = spawnSync(
  "docker",
  [
    "run",
    "--rm",
    "-p",
    "4173:80",
    "-v",
    `${distPath}:/usr/share/nginx/html:ro`,
    "-v",
    `${configPath}:/etc/nginx/conf.d/default.conf:ro`,
    "nginx:alpine",
  ],
  { stdio: "inherit" }
);
process.exit(docker.status ?? 0);
