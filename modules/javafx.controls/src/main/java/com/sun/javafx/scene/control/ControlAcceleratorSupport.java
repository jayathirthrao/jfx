/*
 * Copyright (c) 2014, 2024, Oracle and/or its affiliates. All rights reserved.
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
package com.sun.javafx.scene.control;

import javafx.beans.InvalidationListener;
import javafx.beans.Observable;
import javafx.beans.property.ReadOnlyObjectProperty;
import javafx.beans.value.ChangeListener;
import javafx.collections.ListChangeListener;
import javafx.collections.ObservableList;
import javafx.event.Event;
import javafx.scene.Node;
import javafx.scene.Scene;
import javafx.scene.control.CheckMenuItem;
import javafx.scene.control.Control;
import javafx.scene.control.Menu;
import javafx.scene.control.MenuItem;
import javafx.scene.control.RadioMenuItem;
import javafx.scene.control.Tab;
import javafx.scene.control.TabPane;
import javafx.scene.control.TableColumn;
import javafx.scene.control.TableColumnBase;
import javafx.scene.control.TreeTableColumn;
import javafx.scene.input.KeyCombination;

import java.lang.ref.WeakReference;
import java.util.List;
import java.util.Map;
import java.util.WeakHashMap;

public class ControlAcceleratorSupport {

    // --- Add

    public static void addAcceleratorsIntoScene(ObservableList<MenuItem> items, Tab anchor) {
        // with Tab, we first need to wait until the Tab has a TabPane associated with it
        addAcceleratorsIntoScene(items, (Object)anchor);
    }

    public static void addAcceleratorsIntoScene(ObservableList<MenuItem> items, TableColumnBase<?,?> anchor) {
        // with TableColumnBase, we first need to wait until it has a TableView/TreeTableView associated with it
        addAcceleratorsIntoScene(items, (Object)anchor);
    }

    public static void addAcceleratorsIntoScene(ObservableList<MenuItem> items, Node anchor) {
        // we allow an empty items list as we install listeners later on - if
        // we return on empty, the listener is never installed (leading to JDK-8094496)
        if (items == null/* || items.isEmpty()*/) {
            return;
        }

        if (anchor == null) {
            throw new IllegalArgumentException("Anchor cannot be null");
        }


        WeakReference<ChangeListener<Scene>> listenerW = sceneChangeListenerMap.get(anchor);
        if (listenerW == null || listenerW.get() == null) {
            final Scene scene = anchor.getScene();
            if (scene != null) {
                doAcceleratorInstall(items, scene);
            }
            // Scene change listener is added to the anchor for scenarios like,
            // 1. Installing accelerators when Control is added to Scene
            // 2. Removing accelerators when Control is removed from Scene
            anchor.sceneProperty().addListener(getSceneChangeListener(anchor, items));
        }
    }

    private static void addAcceleratorsIntoScene(ObservableList<MenuItem> items, Object anchor) {
        // with TableColumnBase, we first need to wait until it has a TableView/TreeTableView associated with it
        if (anchor == null) {
            throw new IllegalArgumentException("Anchor cannot be null");
        }

        final ReadOnlyObjectProperty<? extends Control> controlProperty = getControlProperty(anchor);
        final Control control = controlProperty.get();
        if (control == null) {
            controlProperty.addListener(new InvalidationListener() {
                @Override public void invalidated(Observable observable) {
                    final Control control = controlProperty.get();
                    if (control != null) {
                        controlProperty.removeListener(this);
                        addAcceleratorsIntoScene(items, control);
                    }
                }
            });
        } else {
            addAcceleratorsIntoScene(items, control);
        }
    }

    /* It's okay to have the value Weak, because we only remember it to remove the listener later on */
    private static Map<Object, WeakReference<ChangeListener<Scene>>> sceneChangeListenerMap = new WeakHashMap<>();

    private static ChangeListener<Scene> getSceneChangeListener(Object anchor, ObservableList<MenuItem> items) {
        WeakReference<ChangeListener<Scene>> sceneChangeListenerW = sceneChangeListenerMap.get(anchor);
        ChangeListener<Scene> sceneChangeListener = sceneChangeListenerW == null ? null : sceneChangeListenerW.get();
        if (sceneChangeListener == null) {
             sceneChangeListener = (ov, oldScene, newScene) -> {
                if (oldScene != null) {
                    removeAcceleratorsFromScene(items, oldScene);
                }
                if (newScene != null) {
                    doAcceleratorInstall(items, newScene);
                }
            };
            sceneChangeListenerMap.put(anchor, new WeakReference<>(sceneChangeListener));
        }
        return sceneChangeListener;
    }

    /* It's okay to have the value Weak, because we only remember it to remove the listener later on */
    private static Map<ListChangeListener<MenuItem>, WeakReference<ListChangeListener<MenuItem>>>
            menuListChangeListenerMap = new WeakHashMap<>();

    private static void doAcceleratorInstall(final ObservableList<MenuItem> items, final Scene scene) {
        // we're given an observable list of menu items, which we will add an observer to
        // so that when menu items are added or removed we can properly handle
        // the addition or removal of accelerators into the scene.
        // We need to store the ListChangeListener for later so that we can clean up, so ideally we'd use the items
        // list as a key in a WeakHashMap with the ListChangeListener as the value. Unfortunately items can't be used
        // as a key as is because its equals method compares the list contents which breaks immediately for empty
        // lists.
        // Instead we have to use an identity wrapper over the items as a key, but since the HashMap is weak, we also
        // have to retain a reference to the identity wrapper class. This could be avoided if there was a
        // WeakIdentityHashMap class available.
        // One way is to use the ListChangeListener itself as the identity wrapper: IdentityWrapperListChangeListener
        // since the items list will have a reference to it. We can create temporary instances of the
        // IdentityWrapperListChangeListener when we need to retrieve items from the WeakHashMap to get the real one.
        ListChangeListener<MenuItem> listChangeListener = new IdentityWrapperListChangeListener(items) {
            @Override
            public void onChanged(Change<? extends MenuItem> c) {
                while (c.next()) {
                    if (c.wasRemoved()) {
                        removeAcceleratorsFromScene(c.getRemoved(), scene);
                    }

                    if (c.wasAdded()) {
                        doAcceleratorInstall(c.getAddedSubList(), scene);
                    }
                }
            }
        };

        // There should only ever be one ListChangeListener from ControlAcceleratorSupport on each items list
        // If there is one somehow, this removes it from the list, and replaces the entry in the HashMap.
        WeakReference<ListChangeListener<MenuItem>> previousW = menuListChangeListenerMap.get(listChangeListener);
        ListChangeListener<MenuItem> previous = previousW == null ? null : previousW.get();
        if (previous != null) {
            items.removeListener(previous);
        }

        menuListChangeListenerMap.put(listChangeListener, new WeakReference<>(listChangeListener));
        items.addListener(listChangeListener);
        doAcceleratorInstall((List<MenuItem>)items, scene);
    }


    private static void doAcceleratorInstall(final List<? extends MenuItem> items, final Scene scene) {
        for (final MenuItem menuitem : items) {
            if (menuitem instanceof Menu) {
                // add accelerators for this Menu's menu items, by calling recursively.
                doAcceleratorInstall(((Menu) menuitem).getItems(), scene);
            } else {
                // check if there is any accelerator on this menuitem right now.
                // If there is, then we create a Runnable and set it into the
                // scene straight away
                if (menuitem.getAccelerator() != null) {
                    final Map<KeyCombination, Runnable> accelerators = scene.getAccelerators();

                    Runnable acceleratorRunnable = () -> {
                        if (menuitem.getOnMenuValidation() != null) {
                            Event.fireEvent(menuitem, new Event(MenuItem.MENU_VALIDATION_EVENT));
                        }
                        Menu target = menuitem.getParentMenu();
                        if(target!= null && target.getOnMenuValidation() != null) {
                            Event.fireEvent(target, new Event(MenuItem.MENU_VALIDATION_EVENT));
                        }
                        if (!menuitem.isDisable()) {
                            if (menuitem instanceof RadioMenuItem radioMenuItem) {
                                if (radioMenuItem.getToggleGroup() == null) {
                                    radioMenuItem.setSelected(!radioMenuItem.isSelected());
                                }
                                else {
                                    radioMenuItem.setSelected(true);
                                }
                            }
                            else if (menuitem instanceof CheckMenuItem) {
                                ((CheckMenuItem)menuitem).setSelected(!((CheckMenuItem)menuitem).isSelected());
                            }

                            menuitem.fire();
                        }
                    };
                    accelerators.put(menuitem.getAccelerator(), acceleratorRunnable);
                }

                // We also listen to the accelerator property for changes, such
                // that we can update the scene when a menu item accelerator changes.
                menuitem.acceleratorProperty().addListener(getListener(scene, menuitem));
            }
        }
    }

    /* It's okay to have the value Weak, because we only remember it to remove the listener later on */
    private static Map<MenuItem, WeakReference<ChangeListener<KeyCombination>>> changeListenerMap = new WeakHashMap<>();

    private static ChangeListener<KeyCombination> getListener(final Scene scene, MenuItem menuItem) {

        WeakReference<ChangeListener<KeyCombination>> listenerW = changeListenerMap.get(menuItem);
        ChangeListener<KeyCombination> listener = listenerW == null ? null : listenerW.get();
        if (listener == null) {
            listener = (observable, oldValue, newValue) -> {
                final Map<KeyCombination, Runnable> accelerators = scene.getAccelerators();

                // remove the old KeyCombination from the accelerators map
                Runnable _acceleratorRunnable = accelerators.remove(oldValue);

                // and put in the new accelerator KeyCombination, if it is not null
                if (newValue != null) {
                    accelerators.put(newValue, _acceleratorRunnable);
                }
            };
            changeListenerMap.put(menuItem, new WeakReference<>(listener));
        }
        return listener;
    }

    // --- Remove

    public static void removeAcceleratorsFromScene(ObservableList<? extends MenuItem> items, Tab anchor) {
        TabPane tabPane = anchor.getTabPane();
        if (tabPane == null) return;

        Scene scene = tabPane.getScene();
        removeAcceleratorsFromScene(items, scene);
    }

    public static void removeAcceleratorsFromScene(ObservableList<? extends MenuItem> items, TableColumnBase<?,?> anchor) {
        ReadOnlyObjectProperty<? extends Control> controlProperty = getControlProperty(anchor);
        if (controlProperty == null) return;

        Control control = controlProperty.get();
        if (control == null) return;

        Scene scene = control.getScene();
        removeAcceleratorsFromScene(items, scene);
    }

    public static void removeAcceleratorsFromScene(ObservableList<? extends MenuItem> items, Node anchor) {
        Scene scene = anchor.getScene();
        if (scene == null) {
            // The Node is not part of a Scene: Remove the Scene listener that was added
            // at the time of installing the accelerators.
            WeakReference<ChangeListener<Scene>> listenerW = sceneChangeListenerMap.get(anchor);
            if (listenerW  != null) {
                ChangeListener<Scene> listener = listenerW.get();
                if (listener != null) {
                    anchor.sceneProperty().removeListener(listener);
                }
                sceneChangeListenerMap.remove(anchor);
            }
        }
        removeAcceleratorsFromScene(items, scene);
    }

    public static void removeAcceleratorsFromScene(ObservableList<? extends MenuItem> items, Scene scene) {
        // use a temporary IdentityWrapperListChangeListener to get the actual one that's listening to items
        WeakReference<ListChangeListener<MenuItem>> listenerW =
                menuListChangeListenerMap.get(new IdentityWrapperListChangeListener(items));
        if (listenerW != null) {
            ListChangeListener<MenuItem> listChangeListener = listenerW.get();
            if (listChangeListener != null) {
                items.removeListener(listChangeListener);
            }
        }
        removeAcceleratorsFromScene((List<MenuItem>)items, scene);
    }

    private static void removeAcceleratorsFromScene(List<? extends MenuItem> items, Scene scene) {
        if (scene == null) {
            return;
        }

        for (final MenuItem menuitem : items) {
            if (menuitem instanceof Menu) {
                // MenuBarSkin uses MenuBarButton to display a Menu.
                // The listener that is added on the 'items' in the method
                // doAcceleratorInstall(final ObservableList<MenuItem> items, final Scene scene)
                // is added to the MenuBarButton.getItems() and not to Menu.getItems().
                // If a Menu is removed from scenegraph then it's skin gets disposed(), which disposes the
                // related MenuBarButton. So it is not required to remove the listener that was added
                // to MenuBarButton.getItems().

                // remove the accelerators of items contained within the menu
                removeAcceleratorsFromScene(((Menu)menuitem).getItems(), scene);
            } else {
                // remove the removed MenuItem accelerator KeyCombination from
                // the scene accelerators map
                final Map<KeyCombination, Runnable> accelerators = scene.getAccelerators();
                accelerators.remove(menuitem.getAccelerator());

                WeakReference<ChangeListener<KeyCombination>> listenerW = changeListenerMap.get(menuitem);
                if (listenerW  != null) {
                    ChangeListener<KeyCombination> listener = listenerW.get();
                    if (listener != null) {
                        menuitem.acceleratorProperty().removeListener(listener);
                    }
                    changeListenerMap.remove(menuitem);
                }
            }
        }
    }



    // --- Utilities

    private static ReadOnlyObjectProperty<? extends Control> getControlProperty(Object obj) {
        if (obj instanceof TableColumn) {
            return ((TableColumn)obj).tableViewProperty();
        } else if (obj instanceof TreeTableColumn) {
            return ((TreeTableColumn)obj).treeTableViewProperty();
        } else if (obj instanceof Tab) {
            return ((Tab)obj).tabPaneProperty();
        }

        return null;
    }

    /**
     * We need to store all the listeners added to each ObservableList so that we can remove them. For this we need
     * a map mapping ObservableList to various listeners such as ListChangeListeners, but the map needs to be a
     * WeakHashMap.
     * The ideal key to the WeakHashMap would be the ObservableList itself, except for the fact that its equals method
     * compares the list contents. If a WeakIdentityHashMap existed, we could use that instead with the ObservableList
     * as the key.
     * We can't use an IdentityWrapper as the key to the HashMap because we need a strong reference (ideally from the
     * ObservableList itself) to the IdentityWrapper else it'll be garbage collected.
     * Since every ObservableList gets a ListChangeListener, we can use that as an IdentityWrapper and rely on the fact
     * ObservableList has a strong reference to the ListChangeListener.
     */
    static class IdentityWrapperListChangeListener implements ListChangeListener<MenuItem> {

        ObservableList<? extends MenuItem> innerList;

        public IdentityWrapperListChangeListener(ObservableList<? extends MenuItem> list) {
            this.innerList = list;
        }

        @Override
        public void onChanged(Change<? extends MenuItem> c) {
            // In some cases this class is used as a key to fetch an item in a HashMap only, and then discarded
            // In other cases the onChanged method is overridden and this is used as a ListChangeListener
        }

        @Override
        public int hashCode() {
            return System.identityHashCode(innerList);
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof IdentityWrapperListChangeListener)) {
                return false;
            }
            IdentityWrapperListChangeListener that = (IdentityWrapperListChangeListener) o;
            return innerList == that.innerList;
        }
    };
}


