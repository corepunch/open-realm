package org.openrealm.wc3;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Arrays;
import java.util.Locale;

/** Per-file transaction: incomplete or invalid copies never replace a working archive. */
public final class ArchiveStore {
    public interface Validator { boolean valid(String path); }

    public static String archiveName(String name) throws IOException {
        if (name == null) throw new IOException("Selected file has no name");
        String lower = name.toLowerCase(Locale.ROOT);
        if (!Arrays.asList("war3.mpq", "war3local.mpq", "war3x.mpq", "war3xlocal.mpq",
                "war3patch.mpq", "war3xpatch.mpq").contains(lower))
            throw new IOException("Unsupported archive: " + name);
        return lower;
    }

    public static long copy(File directory, String name, InputStream input, long expected, Validator validator)
            throws IOException {
        File temporary = null;
        try {
            String target;
            long copied = 0;
            try (InputStream source = input) {
                target = archiveName(name);
                if (source == null) throw new IOException("Cannot open " + name);
                if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("Cannot create data directory");
                if (expected > directory.getUsableSpace()) throw new IOException("Not enough free space for " + name);
                temporary = File.createTempFile(".import-", ".tmp", directory);
                try (FileOutputStream output = new FileOutputStream(temporary)) {
                    byte[] buffer = new byte[1024 * 1024];
                    int count;
                    while ((count = source.read(buffer)) != -1) {
                        output.write(buffer, 0, count);
                        copied += count;
                    }
                    output.getFD().sync();
                }
            }
            // Provider close errors must occur before committing the replacement.
            if (copied == 0 || (expected >= 0 && copied != expected)) throw new IOException("Incomplete archive: " + name);
            if (!validator.valid(temporary.getAbsolutePath())) throw new IOException("Invalid MPQ archive: " + name);
            Files.move(temporary.toPath(), new File(directory, target).toPath(),
                StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            return copied;
        } finally {
            if (temporary != null) Files.deleteIfExists(temporary.toPath());
        }
    }
}
