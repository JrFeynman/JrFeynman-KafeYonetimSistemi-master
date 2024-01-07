import javax.swing.*;
import java.io.File;
import java.util.List;
public interface IMenu {
    void readMenuFromFile(String path, List<Urun> list, DefaultListModel<String> listModel);
    void openAndModifyMenuFile();
    void modifyMenuFile(File file);
}
