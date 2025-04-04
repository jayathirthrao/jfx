/*
 * Copyright (c) 2011, 2024, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

package com.sun.javafx.tk.quantum;

import java.io.IOException;
import java.io.InputStream;

import com.sun.javafx.iio.ImageFrame;
import com.sun.javafx.iio.ImageLoadListener;
import com.sun.javafx.iio.ImageLoader;
import com.sun.javafx.iio.ImageMetadata;
import com.sun.javafx.iio.ImageStorage;
import com.sun.javafx.iio.ImageStorageException;
import com.sun.javafx.runtime.async.AbstractRemoteResource;
import com.sun.javafx.runtime.async.AsyncOperationListener;
import com.sun.javafx.tk.PlatformImage;
import com.sun.prism.Image;
import com.sun.prism.impl.PrismSettings;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import com.sun.javafx.logging.PlatformLogger;

class PrismImageLoader2 implements com.sun.javafx.tk.ImageLoader {

    private static PlatformLogger imageioLogger = null;

    private Image[] images;
    private int[] delayTimes;
    private int loopCount;
    private double width;
    private double height;
    private float pixelScale;
    private Exception exception;

    public PrismImageLoader2(String url, double width, double height,
                             boolean preserveRatio, float pixelScale,
                             boolean smooth)
    {
        loadAll(url, width, height, preserveRatio, pixelScale, smooth);
    }

    public PrismImageLoader2(InputStream stream, double width, double height,
                             boolean preserveRatio, boolean smooth)
    {
        loadAll(stream, width, height, preserveRatio, smooth);
    }

    @Override
    public double getWidth() {
        return width;
    }

    @Override
    public double getHeight() {
        return height;
    }

    @Override
    public int getFrameCount() {
        if (images == null) {
            return 0;
        }
        return images.length;
    }

    @Override
    public PlatformImage getFrame(int index) {
        if (images == null) {
            return null;
        }
        return images[index];
    }

    @Override
    public int getFrameDelay(int index) {
        if (images == null) {
            return 0;
        }
        return delayTimes[index];
    }

    @Override
    public int getLoopCount() {
        if (images == null) {
            return 0;
        }
        return loopCount;
    }

    @Override
    public Exception getException() {
        return exception;
    }

    private void loadAll(String url, double w, double h,
                         boolean preserveRatio, float pixelScale,
                         boolean smooth)
    {
        ImageLoadListener listener = new PrismLoadListener();
        try {
            ImageFrame[] imgFrames =
                ImageStorage.getInstance().loadAll(url, listener, w, h, preserveRatio, pixelScale, smooth);
            convertAll(imgFrames);
        } catch (ImageStorageException e) {
            handleException(e);
        } catch (Exception e) {
            handleException(e);
        }
    }

    private void loadAll(InputStream stream, double w, double h,
                         boolean preserveRatio, boolean smooth)
    {
        ImageLoadListener listener = new PrismLoadListener();
        try {
            ImageFrame[] imgFrames =
                ImageStorage.getInstance().loadAll(stream, listener, w, h, preserveRatio, 1.0f, smooth);
            convertAll(imgFrames);
        } catch (ImageStorageException e) {
            handleException(e);
        } catch (Exception e) {
            handleException(e);
        }
    }

    private void handleException(final ImageStorageException isException) {
        // unwrap ImageStorageException if possible
        final Throwable exceptionCause = isException.getCause();
        if (exceptionCause instanceof Exception) {
            handleException((Exception) exceptionCause);
        } else {
            handleException((Exception) isException);
        }
    }

    private void handleException(final Exception exception) {
        if (PrismSettings.verbose) {
            exception.printStackTrace(System.err);
        }
        this.exception = exception;
    }

    private void convertAll(ImageFrame[] imgFrames) {
        int numFrames = imgFrames.length;
        images = new Image[numFrames];
        delayTimes = new int[numFrames];
        for (int i = 0; i < numFrames; i++) {
            ImageFrame frame = imgFrames[i];
            images[i] = com.sun.prism.Image.convertImageFrame(frame);
            ImageMetadata metadata = frame.getMetadata();
            if (metadata != null) {
                Integer delay = metadata.delayTime;
                if (delay != null) {
                    delayTimes[i] = delay.intValue();
                }
                Integer loopCount = metadata.loopCount;
                if (loopCount != null) {
                    this.loopCount = loopCount;
                }
            }
            if (i == 0) {
                width = frame.getWidth();
                height = frame.getHeight();
            }
        }
    }

    /**
     * Returns the PlatformLogger for logging imageio-related activities.
     */
    private static synchronized PlatformLogger getImageioLogger() {
        if (imageioLogger == null) {
            imageioLogger = PlatformLogger.getLogger("javafx.scene.image");
        }

        return imageioLogger;
    }

    private class PrismLoadListener implements ImageLoadListener {
        @Override
        public void imageLoadWarning(ImageLoader loader, String message) {
            getImageioLogger().warning(message);
        }

        @Override
        public void imageLoadProgress(ImageLoader loader,
                                      float percentageComplete)
        {
            // progress only matters when backgroundLoading=true, but
            // currently we are relying on AbstractRemoteResource for tracking
            // progress of the InputStream, so there's no need to implement
            // this for now; eventually though we might want to consider
            // moving away from AbstractRemoteResource and instead use
            // the built-in support for progress in the javafx-iio library...
        }

        @Override
        public void imageLoadMetaData(ImageLoader loader, ImageMetadata metadata) {
            // We currently have no need to listen for ImageMetadata ready.
        }
    }

    static final class AsyncImageLoader
        extends AbstractRemoteResource<PrismImageLoader2>
    {
        private static final ExecutorService BG_LOADING_EXECUTOR =
                createExecutor();

        double width, height;
        boolean preserveRatio;
        boolean smooth;

        public AsyncImageLoader(
                AsyncOperationListener<PrismImageLoader2> listener,
                String url,
                double width, double height, boolean preserveRatio, boolean smooth)
        {
            super(url, listener);
            this.width = width;
            this.height = height;
            this.preserveRatio = preserveRatio;
            this.smooth = smooth;
        }

        @Override
        protected PrismImageLoader2 processStream(InputStream stream) throws IOException {
            return new PrismImageLoader2(stream, width, height, preserveRatio, smooth);
        }

        @Override
        public PrismImageLoader2 call() throws IOException {
            return AsyncImageLoader.super.call();
        }

        @Override
        public void start() {
            BG_LOADING_EXECUTOR.execute(future);
        }

        private static ExecutorService createExecutor() {
            final ThreadGroup bgLoadingThreadGroup =
                    new ThreadGroup(QuantumToolkit.getFxUserThread()
                            .getThreadGroup(),
                            "Background image loading thread pool");

            final ThreadFactory bgLoadingThreadFactory = runnable -> {
                final Thread newThread
                        = new Thread(bgLoadingThreadGroup,
                                runnable);
                newThread.setPriority(
                        Thread.MIN_PRIORITY);

                return newThread;
            };

            final ExecutorService bgLoadingExecutor =
                    Executors.newCachedThreadPool(bgLoadingThreadFactory);
            ((ThreadPoolExecutor) bgLoadingExecutor).setKeepAliveTime(
                                                         1, TimeUnit.SECONDS);

            return bgLoadingExecutor;
        }
    }
}
