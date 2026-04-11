import { readdirSync, renameSync, copyFileSync, statSync, existsSync } from "fs";
import { join } from "path";

function dot() {
  dot();
  // @ts-ignore - Bun-specific API for flushing stdout
  if (typeof Bun !== "undefined") Bun.stdout.flush?.();
}

const dirs = ["X:\\sumtest\\bugs\\", "C:\\Users\\kjk\\OneDrive\\!sumatra\\bugs\\"];

// --- Step 1: rename "bug<number><rest>" to "bug-<number><rest>" ---

const reBugNoHyphen = /^bug(\d+)(.*)/i;

function renameBugFiles(dir: string) {
  console.log(`renameBugFiles: ${dir}`);
  if (!existsSync(dir)) {
    console.log(`skipping ${dir} (does not exist)`);
    return;
  }
  const entries = readdirSync(dir);
  let nRenamed = 0;
  let nSeen = 0;
  for (const name of entries) {
    nSeen++;
    if (nSeen % 16 === 0) {
      dot();
    }
    const m = name.match(reBugNoHyphen);
    if (!m) {
      continue;
    }
    const newName = `bug-${m[1]}${m[2]}`;
    if (newName === name) {
      continue;
    }
    const oldPath = join(dir, name);
    const newPath = join(dir, newName);
    if (existsSync(newPath)) {
      console.log(`skipping ${name} -> ${newName} (target already exists)`);
      continue;
    }
    console.log(`${name} -> ${newName}`);
    renameSync(oldPath, newPath);
    nRenamed++;
  }
  if (nSeen > 0) {
    console.log(""); // newline after dots
  }
  if (nRenamed > 0) {
    console.log(`${dir}: renamed ${nRenamed} files`);
  }
}

// --- Step 2: sync directories ---

interface BugFileInfo {
  bugNumber: number;
  fileName: string;
  fileSize: number;
}

const reBugFile = /^bug-(\d+)(.*)/i;

function collectBugFiles(dir: string): BugFileInfo[] {
  console.log(`collectBugFiles: ${dir}`);
  if (!existsSync(dir)) {
    console.log(`collectBugFiles: ${dir} does not exist, returning empty`);
    return [];
  }
  const entries = readdirSync(dir);
  const result: BugFileInfo[] = [];
  for (const name of entries) {
    const m = name.match(reBugFile);
    if (!m) {
      continue;
    }
    const fullPath = join(dir, name);
    const st = statSync(fullPath);
    if (!st.isFile()) {
      continue;
    }
    result.push({
      bugNumber: parseInt(m[1], 10),
      fileName: name,
      fileSize: st.size,
    });
    if (result.length % 16 === 0) {
      dot();
    }
  }
  if (result.length > 0) {
    console.log(""); // newline after dots
  }
  console.log(`collectBugFiles: ${dir} done, ${result.length} files`);
  return result;
}

function syncDirs(dirs: string[]) {
  // collect file info for each directory
  const dirFiles: Map<string, BugFileInfo[]> = new Map();
  for (const dir of dirs) {
    dirFiles.set(dir, collectBugFiles(dir));
  }

  let nCopied = 0;
  // for each directory, check each file against all other directories
  for (const srcDir of dirs) {
    console.log(`syncDirs: processing srcDir ${srcDir}`);
    const srcFiles = dirFiles.get(srcDir)!;
    for (const srcFile of srcFiles) {
      for (const dstDir of dirs) {
        if (dstDir === srcDir) {
          continue;
        }
        const dstFiles = dirFiles.get(dstDir)!;
        // look for a file with same bug number and file size
        const match = dstFiles.find((f) => f.bugNumber === srcFile.bugNumber && f.fileSize === srcFile.fileSize);
        if (match) {
          if (match.fileName !== srcFile.fileName) {
            console.log(
              `note: same bug #${srcFile.bugNumber}, same size, different names: "${srcFile.fileName}" vs "${match.fileName}"`,
            );
          }
          continue;
        }
        // check if a file with the same name already exists (different size = conflict)
        const dstPath = join(dstDir, srcFile.fileName);
        if (existsSync(dstPath)) {
          const dstSt = statSync(dstPath);
          console.log(
            `conflict: ${srcFile.fileName} exists in ${dstDir} with different size (${srcFile.fileSize} vs ${dstSt.size}), skipping`,
          );
          continue;
        }
        // copy the file
        const srcPath = join(srcDir, srcFile.fileName);
        console.log(`copy: ${srcFile.fileName} -> ${dstDir}`);
        copyFileSync(srcPath, dstPath);
        // add to dstFiles so we don't copy it again from another source
        dstFiles.push({
          bugNumber: srcFile.bugNumber,
          fileName: srcFile.fileName,
          fileSize: srcFile.fileSize,
        });
        nCopied++;
      }
    }
  }
  console.log(`sync: copied ${nCopied} files`);
}

export function run() {
  for (const dir of dirs) {
    renameBugFiles(dir);
  }
  syncDirs(dirs);
}

run(); // uncomment or call run() to execute
